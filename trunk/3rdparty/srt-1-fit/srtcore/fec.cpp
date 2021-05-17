/*
 * SRT - Secure, Reliable, Transport
 * Copyright (c) 2019 Haivision Systems Inc.
 * 
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 * 
 */


#include <string>
#include <map>
#include <vector>
#include <deque>

#include "packetfilter.h"
#include "core.h"
#include "packet.h"
#include "logging.h"

#include "fec.h"

using namespace std;
using namespace srt_logging;

FECFilterBuiltin::FECFilterBuiltin(const SrtFilterInitializer &init, std::vector<SrtPacket> &provided, const string &confstr)
    : SrtPacketFilterBase(init)
    , m_fallback_level(SRT_ARQ_ONREQ)
    , m_arrangement_staircase(true)
    , rcv(provided)
{
    if (!ParseFilterConfig(confstr, cfg))
        throw CUDTException(MJ_NOTSUP, MN_INVAL, 0);

    // Configuration supported:
    // - row only (number_rows == 1)
    // - columns only, no row FEC/CTL (number_rows < -1)
    // - columns and rows (both > 1)

    // Disallowed configurations:
    // - number_cols < 1
    // - number_rows [-1, 0]

    string arspec = map_get(cfg.parameters, "layout");

    string shorter = arspec.size() > 5 ? arspec.substr(0, 5) : arspec;
    if (shorter == "even")
        m_arrangement_staircase = false;
    else if (shorter != "" && shorter != "stair")
    {
        LOGC(mglog.Error, log << "FILTER/FEC: CONFIG: value for 'layout' must be 'even' or 'staircase'");
        throw CUDTException(MJ_NOTSUP, MN_INVAL, 0);
    }

    string colspec = map_get(cfg.parameters, "cols"), rowspec = map_get(cfg.parameters, "rows");

    int out_rows = 1;
    int out_cols = atoi(colspec.c_str());

    if (colspec == "" || out_cols < 2)
    {
        LOGC(mglog.Error, log << "FILTER/FEC: CONFIG: at least 'cols' must be specified and > 1");
        throw CUDTException(MJ_NOTSUP, MN_INVAL, 0);
    }

    m_number_cols = out_cols;

    if (rowspec != "")
    {
        out_rows = atoi(rowspec.c_str());
        if (out_rows >= -1 && out_rows < 1)
        {
            LOGC(mglog.Error, log << "FILTER/FEC: CONFIG: 'rows' must be >=1 or negative < -1");
            throw CUDTException(MJ_NOTSUP, MN_INVAL, 0);
        }
    }

    if (out_rows < 0)
    {
        m_number_rows = -out_rows;
        m_cols_only = true;
    }
    else
    {
        m_number_rows = out_rows;
        m_cols_only = false;
    }

    // Extra interpret level, if found, default never.
    // Check only those that are managed.
    string level = cfg.parameters["arq"];
    int lv = -1;
    if (level != "")
    {
        static const char* levelnames [] = { "never", "onreq", "always" };

        for (size_t i = 0; i < Size(levelnames); ++i)
        {
            if (level == levelnames[i])
            {
                lv = i;
                break;
            }
        }

        if (lv == -1)
        {
            LOGC(mglog.Error, log << "FILTER/FEC: CONFIG: 'arq': value '" << level << "' unknown");
            throw CUDTException(MJ_NOTSUP, MN_INVAL, 0);
        }

        m_fallback_level = SRT_ARQLevel(lv);
    }
    else
    {
        m_fallback_level = SRT_ARQ_ONREQ;
    }


    // Required to store in the header when rebuilding
    rcv.id = socketID();

    // Setup the bit matrix, initialize everything with false.

    // Vertical size (y)
    rcv.cells.resize(sizeCol() * sizeRow(), false);

    // These sequence numbers are both the value of ISN-1 at the moment
    // when the handshake is done. The sender ISN is generated here, the
    // receiver ISN by the peer. Both should be known after the handshake.
    // Later they will be updated as packets are transmitted.

    int32_t snd_isn = CSeqNo::incseq(sndISN());
    int32_t rcv_isn = CSeqNo::incseq(rcvISN());

    // Alright, now we need to get the ISN from m_parent
    // to extract the sequence number allowing qualification to the group.
    // The base values must be prepared so that feedSource can qualify them.

    // SEPARATE FOR SENDING AND RECEIVING!

    // Now, assignment of the groups requires:
    // For row groups, simply the size of the group suffices.
    // For column groups, you need a whole matrix of all sequence
    // numbers that are base sequence numbers for the group.
    // Sequences that belong to this group are:
    // 1. First packet has seq+1 towards the base.
    // 2. Every next packet has this value + the size of the row group.
    // So: group dispatching is:
    //  - get the column number
    //  - extract the group data for that column
    //  - check if the sequence is later than the group base sequence, if not, report no group for the packet
    //  - sanity check, if the seqdiff divided by row size gets 0 remainder
    //  - The result from the above division can't exceed the column size, otherwise
    //    it's another group. The number of currently collected data should be in 'collected'.

    // Now set up the group starting sequences.
    // The very first group in both dimensions will have the value of ISN in particular direction.

    // Set up sender part.
    //
    // Size: rows
    // Step: 1 (next packet in group is 1 past the previous one)
    // Slip: rows (first packet in the next group is distant to first packet in the previous group by 'rows')
    HLOGC(mglog.Debug, log << "FEC: INIT: ISN { snd=" << snd_isn << " rcv=" << rcv_isn << " }; sender single row");
    ConfigureGroup(snd.row, snd_isn, 1, sizeRow());

    // In the beginning we need just one reception group. New reception
    // groups will be created in tact with receiving packets outside this one.
    // The value of rcv.row[0].base will be used as an absolute base for calculating
    // the index of the group for a given received packet.
    rcv.rowq.resize(1);
    HLOGP(mglog.Debug, "FEC: INIT: receiver first row");
    ConfigureGroup(rcv.rowq[0], rcv_isn, 1, sizeRow());

    if (sizeCol() > 1)
    {
        // Size: cols
        // Step: rows (the next packet in the group is one row later)
        // Slip: rows+1 (the first packet in the next group is later by 1 column + one whole row down)

        HLOGP(mglog.Debug, "FEC: INIT: sender first N columns");
        ConfigureColumns(snd.cols, snd_isn);
        HLOGP(mglog.Debug, "FEC: INIT: receiver first N columns");
        ConfigureColumns(rcv.colq, rcv_isn);
    }

    // The bit markers that mark the received/lost packets will be expanded
    // as packets come in.
    rcv.cell_base = rcv_isn;
}

template <class Container>
void FECFilterBuiltin::ConfigureColumns(Container& which, int32_t isn)
{
    // This is to initialize the first set of groups.

    // which: group vector.
    // numberCols(): number of packets in one group
    // sizeCol(): seqdiff between two packets consecutive in the group
    // m_column_slip: seqdiff between the first packet in one group and first packet in the next group
    // isn: sequence number of the first packet in the first group

    size_t zero = which.size();

    // The first series of initialization should embrace:
    // - if multiplyer == 1, EVERYTHING (also the case of SOLID matrix)
    // - if more, ONLY THE FIRST SQUARE.
    which.resize(zero + numberCols());

    if (!m_arrangement_staircase)
    {
        HLOGC(mglog.Debug, log << "ConfigureColumns: new "
                << numberCols() << " columns, START AT: " << zero);
        // With even arrangement, just use a plain loop.
        // Initialize straight way all groups in the size.
        int32_t seqno = isn;
        for (size_t i = zero; i < which.size(); ++i)
        {
            // ARGS:
            // - seqno: sequence number of the first packet in the group
            // - step: distance between two consecutive packets in the group
            // - drop: distance between base sequence numbers in groups in consecutive series
            // (meaning: with row size 6, group with index 2 and 8 are in the
            // same column 2, lying in 0 and 1 series respectively).
            ConfigureGroup(which[i], seqno, sizeRow(), sizeCol() * numberCols());
            seqno = CSeqNo::incseq(seqno);
        }
        return;
    }

    // With staircase, the next column's base sequence is
    // shifted by 1 AND the length of the row. When this shift
    // becomes below the column 0 bottom, reset it to the row 0
    // and continue.

    // Start here. The 'isn' is still the absolute base sequence value.
    size_t offset = 0;

    HLOGC(mglog.Debug, log << "ConfigureColumns: " << (which.size() - zero)
            << " columns, START AT: " << zero);

    for (size_t i = zero; i < which.size(); ++i)
    {
        int32_t seq = CSeqNo::incseq(isn, offset);
        size_t col = i - zero;

        HLOGC(mglog.Debug, log << "ConfigureColumns: [" << col << "]: -> ConfigureGroup...");
        ConfigureGroup(which[i], seq, sizeRow(), sizeCol() * numberCols());

        if (col % numberRows() == numberRows() - 1)
        {
            offset = col + 1; // +1 because we want it for the next column
            HLOGC(mglog.Debug, log << "ConfigureColumns: [" << (col+1) << "]... (resetting to row 0: +"
                    << offset << " %" << CSeqNo::incseq(isn, offset) << ")");
        }
        else
        {
            offset += 1 + sizeRow();
            HLOGC(mglog.Debug, log << "ConfigureColumns: [" << (col+1) << "] ... (continue +"
                    << offset << " %" << CSeqNo::incseq(isn, offset) << ")");
        }
    }
}

void FECFilterBuiltin::ConfigureGroup(Group& g, int32_t seqno, size_t gstep, size_t drop)
{
    g.base = seqno;
    g.step = gstep;

    // This actually rewrites the size of the group here, but
    // by having this value precalculated we simply close the
    // group by adding this value to the base sequence.
    g.drop = drop;
    g.collected = 0;

    // Now the buffer spaces for clips.
    g.payload_clip.resize(payloadSize());
    g.length_clip = 0;
    g.flag_clip = 0;
    g.timestamp_clip = 0;

    HLOGC(mglog.Debug, log << "FEC: ConfigureGroup: base %" << seqno << " step=" << gstep << " drop=" << drop);

    // Preallocate the buffer that will be used for storing it for
    // the needs of passing the data through the network.
    // This will be filled with zeros initially, which is unnecessary,
    // but it happeens just once after connection.
}


void FECFilterBuiltin::ResetGroup(Group& g)
{
    int32_t new_seq_base = CSeqNo::incseq(g.base, g.drop);

    HLOGC(mglog.Debug, log << "FEC: ResetGroup (step=" << g.step << "): base %" << g.base << " -> %" << new_seq_base);

    g.base = new_seq_base;
    g.collected = 0;

    // This isn't necessary for ConfigureGroup because the
    // vector after resizing is filled with a given value,
    // by default the default value of the type, char(), that is 0.
    g.length_clip = 0;
    g.flag_clip = 0;
    g.timestamp_clip = 0;
    memset(&g.payload_clip[0], 0, g.payload_clip.size());
}

void FECFilterBuiltin::feedSource(CPacket& packet)
{
    // Hang on the matrix. Find by packet->getSeqNo().

    //    (The "absolute base" is the cell 0 in vertical groups)
    int32_t base = snd.row.base;

    // (we are guaranteed that this packet is a data packet, so
    // we don't have to check if this isn't a control packet)
    int baseoff = CSeqNo::seqoff(base, packet.getSeqNo());

    int horiz_pos = baseoff;

    if (CheckGroupClose(snd.row, horiz_pos, sizeRow()))
    {
        HLOGC(mglog.Debug, log << "FEC:... HORIZ group closed, B=%" << snd.row.base);
    }
    ClipPacket(snd.row, packet);
    snd.row.collected++;

    // Don't do any column feeding if using column size 1
    if (sizeCol() < 2)
    {
        // The above logging instruction in case of no columns
        HLOGC(mglog.Debug, log << "FEC:feedSource: %" << packet.getSeqNo()
                << " B:%" << baseoff << " H:*[" << horiz_pos << "]"
                << " size=" << packet.size()
                << " TS=" << packet.getMsgTimeStamp()
                << " !" << BufferStamp(packet.data(), packet.size()));
        HLOGC(mglog.Debug, log << "FEC collected: H: " << snd.row.collected);
        return;
    }

    // 1. Get the number of group in both vertical and horizontal groups:
    //    - Vertical: offset towards base (% row size, but with updated Base seq unnecessary)
    // (Just for a case).
    int vert_gx = baseoff % sizeRow();

    // 2. Define the position of this packet in the group
    //    - Horizontal: offset towards base (of the given group, not absolute!)
    //    - Vertical: (seq-base)/column_size
    int32_t vert_base = snd.cols[vert_gx].base;
    int vert_off = CSeqNo::seqoff(vert_base, packet.getSeqNo());

    // It MAY HAPPEN that the base is newer than the sequence of the packet.
    // This may normally happen in the beginning period, where the bases
    // set up initially for all columns got the shift, so they are kinda from
    // the future, and "this sequence" is in a group that is already closed.
    // In this case simply can't clip the packet in the column group.

    HLOGC(mglog.Debug, log << "FEC:feedSource: %" << packet.getSeqNo() << " rowoff=" << baseoff
            << " column=" << vert_gx << " .base=%" << vert_base << " coloff=" << vert_off);

    if (vert_off >= 0 && sizeCol() > 1)
    {
        // BEWARE! X % Y with different signedness upgrades int to unsigned!

        // SANITY: check if the rule applies on the group
        if (vert_off % sizeRow())
        {
            LOGC(mglog.Fatal, log << "FEC:feedSource: IPE: VGroup #" << vert_gx << " base=%" << vert_base
                    << " WRONG with horiz base=%" << base << "coloff(" << vert_off
                    << ") % sizeRow(" << sizeRow() << ") = " << (vert_off % sizeRow()));

            // Do not place it, it would be wrong.
            return;
        }

        int vert_pos = vert_off / sizeRow();

        HLOGC(mglog.Debug, log << "FEC:feedSource: %" << packet.getSeqNo()
                << " B:%" << baseoff << " H:*[" << horiz_pos << "] V(B=%" << vert_base
                << ")[col=" << vert_gx << "][" << vert_pos << "/" << sizeCol() << "] "
                << " size=" << packet.size()
                << " TS=" << packet.getMsgTimeStamp()
                << " !" << BufferStamp(packet.data(), packet.size()));

        // 3. The group should be check for the necessity of being closed.
        // Note that FEC packet extraction doesn't change the state of the
        // VERTICAL groups (it can be potentially extracted multiple times),
        // only the horizontal in order to mark that the vertical FEC is
        // extracted already. So, anyway, check if the group limit was reached
        // and it wasn't closed.
        // 4. Apply the clip
        // 5. Increase collected.

        if (CheckGroupClose(snd.cols[vert_gx], vert_pos, sizeCol()))
        {
            HLOGC(mglog.Debug, log << "FEC:... VERT group closed, B=%" << snd.cols[vert_gx].base);
        }
        ClipPacket(snd.cols[vert_gx], packet);
        snd.cols[vert_gx].collected++;
    }
    else
    {

        HLOGC(mglog.Debug, log << "FEC:feedSource: %" << packet.getSeqNo()
                << " B:%" << baseoff << " H:*[" << horiz_pos << "] V(B=%" << vert_base
                << ")[col=" << vert_gx << "]<NO-COLUMN>"
                << " size=" << packet.size()
                << " TS=" << packet.getMsgTimeStamp()
                << " !" << BufferStamp(packet.data(), packet.size()));
    }
    HLOGC(mglog.Debug, log << "FEC collected: H: " << snd.row.collected << " V[" << vert_gx << "]: " << snd.cols[vert_gx].collected);
}

bool FECFilterBuiltin::CheckGroupClose(Group& g, size_t pos, size_t size)
{
    if (pos < size)
        return false;

    ResetGroup(g);
    return true;
}

void FECFilterBuiltin::ClipPacket(Group& g, const CPacket& pkt)
{
    // Both length and timestamp must be taken as NETWORK ORDER
    // before applying the clip.

    uint16_t length_net = htons(pkt.size());
    uint8_t kflg = uint8_t(pkt.getMsgCryptoFlags());

    // NOTE: Unlike length, the TIMESTAMP is NOT endian-reordered
    // because it will be written into the TIMESTAMP field in the
    // header, and header is inverted automatically when sending,
    // unlike the contents of the payload, where the length will be written.
    uint32_t timestamp_hw = pkt.getMsgTimeStamp();

    ClipData(g, length_net, kflg, timestamp_hw, pkt.data(), pkt.size());

    HLOGC(mglog.Debug, log << "FEC DATA PKT CLIP: " << hex
            << "FLAGS=" << unsigned(kflg) << " LENGTH[ne]=" << (length_net)
            << " TS[he]=" << timestamp_hw
            << " CLIP STATE: FLAGS=" << unsigned(g.flag_clip)
            << " LENGTH[ne]=" << g.length_clip
            << " TS[he]=" << g.timestamp_clip
            << " PL4=" << (*(uint32_t*)&g.payload_clip[0]));
}

// Clipping a control packet does merely the same, just the packet has
// different contents, so it must be differetly interpreted.
void FECFilterBuiltin::ClipControlPacket(Group& g, const CPacket& pkt)
{
    // Both length and timestamp must be taken as NETWORK ORDER
    // before applying the clip.

    const char* fec_header = pkt.data();
    const char* payload = fec_header + 4;
    size_t payload_clip_len = pkt.size() - 4;

    const uint8_t* flag_clip = (const uint8_t*)(fec_header + 1);
    const uint16_t* length_clip = (const uint16_t*)(fec_header + 2);

    uint32_t timestamp_hw = pkt.getMsgTimeStamp();

    ClipData(g, *length_clip, *flag_clip, timestamp_hw, payload, payload_clip_len);

    HLOGC(mglog.Debug, log << "FEC/CTL CLIP: " << hex
            << "FLAGS=" << unsigned(*flag_clip) << " LENGTH[ne]=" << (*length_clip)
            << " TS[he]=" << timestamp_hw
            << " CLIP STATE: FLAGS=" << unsigned(g.flag_clip)
            << " LENGTH[ne]=" << g.length_clip
            << " TS[he]=" << g.timestamp_clip
            << " PL4=" << (*(uint32_t*)&g.payload_clip[0]));
}

void FECFilterBuiltin::ClipRebuiltPacket(Group& g, Receive::PrivPacket& pkt)
{
    uint16_t length_net = htons(pkt.length);
    uint8_t kflg = MSGNO_ENCKEYSPEC::unwrap(pkt.hdr[SRT_PH_MSGNO]);

    // NOTE: Unlike length, the TIMESTAMP is NOT endian-reordered
    // because it will be written into the TIMESTAMP field in the
    // header, and header is inverted automatically when sending,
    // unlike the contents of the payload, where the length will be written.
    uint32_t timestamp_hw = pkt.hdr[SRT_PH_TIMESTAMP];

    ClipData(g, length_net, kflg, timestamp_hw, pkt.buffer, pkt.length);

    HLOGC(mglog.Debug, log << "FEC REBUILT DATA CLIP: " << hex
            << "FLAGS=" << unsigned(kflg) << " LENGTH[ne]=" << (length_net)
            << " TS[he]=" << timestamp_hw
            << " CLIP STATE: FLAGS=" << unsigned(g.flag_clip)
            << " LENGTH[ne]=" << g.length_clip
            << " TS[he]=" << g.timestamp_clip
            << " PL4=" << (*(uint32_t*)&g.payload_clip[0]));
}

void FECFilterBuiltin::ClipData(Group& g, uint16_t length_net, uint8_t kflg,
        uint32_t timestamp_hw, const char* payload, size_t payload_size)
{
    g.length_clip = g.length_clip ^ length_net;
    g.flag_clip = g.flag_clip ^ kflg;
    g.timestamp_clip = g.timestamp_clip ^ timestamp_hw;

    // Payload goes "as is".
    for (size_t i = 0; i < payload_size; ++i)
    {
        g.payload_clip[i] = g.payload_clip[i] ^ payload[i];
    }

    // Fill the rest with zeros. When this packet is going to be
    // recovered, the payload extraced from this process will have
    // the maximum lenght, but it will be cut to the right length
    // and these padding 0s taken out.
    for (size_t i = payload_size; i < payloadSize(); ++i)
        g.payload_clip[i] = g.payload_clip[i] ^ 0;
}

bool FECFilterBuiltin::packControlPacket(SrtPacket& rpkt, int32_t seq)
{
    // If the FEC packet is not yet ready for extraction, do nothing and return false.
    // Check if seq is the last sequence of the group.

    // Check VERTICAL group first, then HORIZONTAL.
    //
    // This is because when it happens that HORIZONTAL group is to be
    // FEC-CTL reported, it also shifts the base to the next row, whereas
    // this base sequence is used to determine the column index that is
    // needed to reach the right column group and it must stay unupdated
    // until the last packet in this row is checked for VERTICAL groups.

    // If it's ready for extraction, extract it, and write into the packet.
    //
    // NOTE: seq is the sequence number of the LAST PACKET SENT regularly.
    // This is only about to be shifted forward by 1 to be placed on the
    // data packet. The packet in `r_packet` doesn't have the sequence number
    // installed yet

    // For BOTH vertical and horizontal snd groups:
    // - Check if the "full group" condition is satisfied (all packets from the group are clipped)
    // - If not, simply return false and do nothing
    // - If so, store the current clip state into the referenced packet, give it the 'seq' sequence

    // After packing the FEC packet:
    // - update the base sequence in the group for which it's packed
    // - make sure that pointers are reset to not suggest the packet is ready

    // Handle the special case of m_number_rows == 1, which
    // means we don't use columns.
    if (m_number_rows <= 1)
    {
        HLOGC(mglog.Debug, log << "FEC/CTL not checking VERT group - rows only config");
        // PASS ON to Horizontal group check
    }
    else
    {
        int offset_to_row_base = CSeqNo::seqoff(snd.row.base, seq);
        int vert_gx = (offset_to_row_base + m_number_cols) % m_number_cols;

        // This can actually happen only for the very first sent packet.
        // It looks like "following the last packet from the previous group",
        // however there was no previous group because this is the first packet.
        if (offset_to_row_base < 0)
        {
            HLOGC(mglog.Debug, log << "FEC/CTL not checking VERT group [" << vert_gx << "] - negative offset_to_row_base %"
                    << snd.row.base << " -> %" << seq << " (" << offset_to_row_base
                    << ") (collected " << snd.cols[abs(vert_gx)].collected << "/" << sizeCol() << ")");
            // PASS ON to Horizontal group check
        }
        else
        {
            if (snd.cols[vert_gx].collected >= m_number_rows)
            {
                HLOGC(mglog.Debug, log << "FEC/CTL ready for VERT group [" << vert_gx << "]: %" << seq
                        << " (base %" << snd.cols[vert_gx].base << ")");
                // SHIP THE VERTICAL FEC packet.
                PackControl(snd.cols[vert_gx], vert_gx, rpkt, seq);

                // RESET THE GROUP THAT WAS SENT
                ResetGroup(snd.cols[vert_gx]);
                return true;
            }

            HLOGC(mglog.Debug, log << "FEC/CTL NOT ready for VERT group [" << vert_gx << "]: %" << seq
                    << " (base %" << snd.cols[vert_gx].base << ")"
                    << " - collected " << snd.cols[vert_gx].collected << "/" << m_number_rows);
        }
    }

    if (snd.row.collected >= m_number_cols)
    {
        if (!m_cols_only)
        {
            HLOGC(mglog.Debug, log << "FEC/CTL ready for HORIZ group: %" << seq << " (base %" << snd.row.base << ")");
            // SHIP THE HORIZONTAL FEC packet.
            PackControl(snd.row, -1, rpkt, seq);

            HLOGC(mglog.Debug, log << "...PACKET size=" << rpkt.length
                    << " TS=" << rpkt.hdr[SRT_PH_TIMESTAMP]
                    << " !" << BufferStamp(rpkt.buffer, rpkt.length));

        }

        // RESET THE HORIZONTAL GROUP.
        // ALWAYS, even in columns-only.
        ResetGroup(snd.row);

        if (!m_cols_only)
        {
            // In columns-only you didn't pack anything, so check
            // for column control.
            return true;
        }
    }
    else
    {
        HLOGC(mglog.Debug, log << "FEC/CTL NOT ready for HORIZ group: %" << seq
                << " (base %" << snd.row.base << ")"
                << " - collected " << snd.row.collected << "/" << m_number_cols);
    }

    return false;
}

void FECFilterBuiltin::PackControl(const Group& g, signed char index, SrtPacket& pkt, int32_t seq)
{
    // Allocate as much space as needed, regardless of the PAYLOADSIZE value.

    static const size_t INDEX_SIZE = 1;

    size_t total_size =
        INDEX_SIZE
        + sizeof(g.flag_clip)
        + sizeof(g.length_clip)
        + g.payload_clip.size();

    // Sanity
#if ENABLE_DEBUG
    if (g.output_buffer.size() < total_size)
    {
        LOGC(mglog.Fatal, log << "OUTPUT BUFFER TOO SMALL!");
        abort();
    }
#endif

    char* out = pkt.buffer;
    size_t off = 0;
    // Spread the index. This is the index of the payload in the vertical group.
    // For horizontal group this value is always -1.
    out[off++] = index;
    // Flags, currently only the encryption flags
    out[off++] = g.flag_clip;

    // Ok, now the length clip
    memcpy(out+off, &g.length_clip, sizeof g.length_clip);
    off += sizeof g.length_clip;

    // And finally the payload clip
    memcpy(out+off, &g.payload_clip[0], g.payload_clip.size());

    // Ready. Now fill the header and finalize other data.
    pkt.length = total_size;

    pkt.hdr[SRT_PH_TIMESTAMP] = g.timestamp_clip;
    pkt.hdr[SRT_PH_SEQNO] = seq;

    HLOGC(mglog.Debug, log << "FEC: PackControl: hdr("
            << (total_size - g.payload_clip.size()) << "): INDEX="
            << int(index) << " LENGTH[ne]=" << hex << g.length_clip
            << " FLAGS=" << int(g.flag_clip) << " TS=" << g.timestamp_clip
            << " PL(" << dec << g.payload_clip.size() << ")[0-4]=" << hex
            << (*(uint32_t*)&g.payload_clip[0]));

}

bool FECFilterBuiltin::receive(const CPacket& rpkt, loss_seqs_t& loss_seqs)
{
    // Add this packet to the group where it belongs.
    // Light up the cell of this packet to mark it received.
    // Check if any of the groups to which the packet belongs
    // have changed the status into RECOVERABLE.
    //
    // The group has RECOVERABLE status when it has FEC
    // packet received and the number of collected packets counts
    // exactly group_size - 1.

    bool want_packet = false;

    struct IsFec
    {
        bool row;
        bool col;
        signed char colx;
    } isfec = { false, false, -1 };

    // The sequence number must be checked prematurely, or it can otherwise
    // cause large resource allocation. This might be even survived, provided
    // that this will make the packet seen as exceeding the series 0 matrix,
    // so all matrices in previous series should be dismissed thereafter. But
    // this short living resource spike may be destructive, so let's do
    // matrix dismissal FIRST before this packet is going to be handled.
    CheckLargeDrop(rpkt.getSeqNo());

    if (rpkt.getMsgSeq() == 0)
    {
        // Interpret the first byte of the contents.
        const char* payload = rpkt.data();
        isfec.colx = payload[0];
        if (isfec.colx == -1)
        {
            isfec.row = true;
        }
        else
        {
            isfec.col = true;
        }

        HLOGC(mglog.Debug, log << "FEC: RECEIVED %" << rpkt.getSeqNo() << " msgno=0, FEC/CTL packet. INDEX=" << int(payload[0]));
    }
    else
    {
        // Data packet, check if this packet was already received.
        // If so, ignore it. This may happen if you have configured
        // FEC and ARQ to cooperate, so a packet once rebuilt might
        // be simultaneously also retransmitted. This may confuse the tables.
        int celloff = CSeqNo::seqoff(rcv.cell_base, rpkt.getSeqNo());
        bool past = celloff < 0;
        bool exists = celloff < int(rcv.cells.size()) && !past && rcv.cells[celloff];

        if (past || exists)
        {
            HLOGC(mglog.Debug, log << "FEC: packet %" << rpkt.getSeqNo() << " "
                    << (past ? "in the PAST" : "already known") << ", IGNORING.");

            return true;
        }

        want_packet = true;

        HLOGC(mglog.Debug, log << "FEC: RECEIVED %" << rpkt.getSeqNo() << " msgno=" << rpkt.getMsgSeq() << " DATA PACKET.");
        MarkCellReceived(rpkt.getSeqNo());
    }

    // Remember this simply every time a packet comes in. In live mode usually
    // this flag is ORD_RELAXED (false), but some earlier versions used ORD_REQUIRED.
    // Even though this flag is now usually ORD_RELAXED, it's fate in live mode
    // isn't completely decided yet, so stay flexible. We believe at least that this
    // flag will stay unchanged during whole connection.
    rcv.order_required = rpkt.getMsgOrderFlag();

    loss_seqs_t irrecover_row, irrecover_col;

    bool ok = true;
    if (!isfec.col) // == regular packet or FEC/ROW
    {
        // Don't manage this packet for horizontal group,
        // if it was a vertical FEC/CTL packet.
        ok = HangHorizontal(rpkt, isfec.row, irrecover_row);
        HLOGC(mglog.Debug, log << "FEC: HangHorizontal %" << rpkt.getSeqNo()
                << " msgno=" << rpkt.getMsgSeq()
                << " RESULT=" << boolalpha << ok << " IRRECOVERABLE: " << Printable(irrecover_row));
    }

    if (!ok)
    {
        // Just informative.
        LOGC(mglog.Warn, log << "FEC/H: rebuilding/hanging FAILED.");
    }

    // Don't do HangVertical in case of row-only configuration
    if (!isfec.row && m_number_rows > 1) // == regular packet or FEC/COL
    {
        ok = HangVertical(rpkt, isfec.colx, irrecover_col);
        HLOGC(mglog.Debug, log << "FEC: HangVertical %" << rpkt.getSeqNo()
                << " msgno=" << rpkt.getMsgSeq()
                << " RESULT=" << boolalpha << ok << " IRRECOVERABLE: " << Printable(irrecover_col));
    }

    if (!ok)
    {
        // Just informative.
        LOGC(mglog.Warn, log << "FEC/V: rebuilding/hanging FAILED.");
    }

    // Pack the following packets as irrecoverable:
    if (m_fallback_level == SRT_ARQ_ONREQ)
    {
        // Use irrecover_row with rows only because there is
        // never anything collected in irrecover_col.
        if (m_number_rows == 1)
            loss_seqs = irrecover_row;
        else
            loss_seqs = irrecover_col;
    }

    return want_packet;
    // Get the packet from the incoming stream, already recognized
    // as data packet, and then:
    //
    // (Note that the default builtin FEC mechanism uses such rules:
    //  - allows SRT to get the packet, even if it follows the loss
    //  - depending on m_fallback_level, confirms or denies the need that SRT handle the loss
    //  - in loss_seqs we return those that are not recoverable at the current level
    //  - FEC has no extra header provided, so regular data are passed as is
    //)
    // So, the needs to implement:
    // 
    // 1. If this is a FEC packet, close the group, check for lost packets, try to recover.
    // Check if there is recovery possible, if so, request a new unit and pack the recovered packet there.
    // Report the loss to be reported by SRT according to m_fallback_level:
    // - ARQ_ALWAYS: N/A for a FEC packet
    // - ARQ_EARLY: When Horizontal group is closed and the packet is not recoverable, report this in loss_seqs
    // - ARQ_LATELY: When Horizontal and Vertical group is closed and the packet is not recoverable, report it.
    // - ARQ_NEVER: Always return empty loss_seqs
    //
    // 2. If this is a regular packet, use it for building the FEC group.
    // - ARQ_ALWAYS: always return true and leave loss_seqs empty.
    // - others: return false and return nothing in loss_seqs
}

void FECFilterBuiltin::CheckLargeDrop(int32_t seqno)
{
    // Ok, first try to pick up the column and series

    int offset = CSeqNo::seqoff(rcv.rowq[0].base, seqno);
    if (offset < 0)
    {
        return;
    }

    // For row-only configuration, check only parts referring
    // to a row.
    if (m_number_rows == 1)
    {
        // We have no columns. So just check if exceeds 5* the row size.
        // If so, clear the rows and reconfigure them.
        if (offset > int(5 * sizeRow()))
        {
            // Calculate the new row base, without breaking the current
            // layout. Make a skip by some number of rows so that the new
            // first row is prepared to receive this packet.

            int32_t oldbase = rcv.rowq[0].base;
            size_t rowdist = offset / sizeRow();
            int32_t newbase = CSeqNo::incseq(oldbase, rowdist * sizeRow());

            LOGC(mglog.Warn, log << "FEC: LARGE DROP detected! Resetting row groups. Base: %" << oldbase
                    << " -> %" << newbase << "(shift by " << CSeqNo::seqoff(oldbase, newbase) << ")");

            rcv.rowq.clear();
            rcv.cells.clear();

            rcv.rowq.resize(1);
            HLOGP(mglog.Debug, "FEC: RE-INIT: receiver first row");
            ConfigureGroup(rcv.rowq[0], newbase, 1, sizeRow());
        }

        return;
    }

    bool reset_anyway = false;
    if (offset != CSeqNo::seqoff(rcv.colq[0].base, seqno))
    {
        reset_anyway = true;
        HLOGC(mglog.Debug, log << "FEC: IPE: row.base %" << rcv.rowq[0].base << " != %" << rcv.colq[0].base << " - resetting");
    }

    // Number of column - regardless of series.
    int colx = offset % numberCols();

    // Base sequence from the group series 0 in this column

    // [[assert rcv.colq.size() >= numberCols()]];
    int32_t colbase = rcv.colq[colx].base;

    // Offset between this base and seqno
    int coloff = CSeqNo::seqoff(colbase, seqno);

    // Might be that it's in the row above the column,
    // still it's not a large-drop
    if (coloff < 0)
    {
        return;
    }

    size_t matrix = numberRows() * numberCols();

    int colseries = coloff / matrix;

    if (colseries > 2 || reset_anyway)
    {
        // Ok, now define the new ABSOLUTE BASE. This is the base of the column 0
        // column group from the series previous towards this one.
        int32_t oldbase = rcv.colq[0].base;
        int32_t newbase = CSeqNo::incseq(oldbase, (colseries-1) * matrix);

        LOGC(mglog.Warn, log << "FEC: LARGE DROP detected! Resetting all groups. Base: %" << oldbase
                << " -> %" << newbase << "(shift by " << CSeqNo::seqoff(oldbase, newbase) << ")");

        rcv.rowq.clear();
        rcv.colq.clear();
        rcv.cells.clear();

        rcv.rowq.resize(1);
        HLOGP(mglog.Debug, "FEC: RE-INIT: receiver first row");
        ConfigureGroup(rcv.rowq[0], newbase, 1, sizeRow());

        // Size: cols
        // Step: rows (the next packet in the group is one row later)
        // Slip: rows+1 (the first packet in the next group is later by 1 column + one whole row down)
        HLOGP(mglog.Debug, "FEC: RE-INIT: receiver first N columns");
        ConfigureColumns(rcv.colq, newbase);

        rcv.cell_base = newbase;
    }
}

void FECFilterBuiltin::CollectIrrecoverRow(RcvGroup& g, loss_seqs_t& irrecover) const
{
    if (g.dismissed)
        return; // already collected

    // Obtain the group's packet shift

    int32_t base = rcv.cell_base;
    int offset = CSeqNo::seqoff(base, g.base);
    if (offset < 0)
    {
        LOGC(mglog.Error, log << "FEC: IPE: row base %" << g.base << " is PAST to cell base %" << base);
        return;
    }

    size_t maxoff = offset + m_number_cols;
    // Sanity check, if all cells are really filled.
    if (maxoff > rcv.cells.size())
    {
        LOGC(mglog.Error, log << "FEC: IPE: Collecting loss from row %"
                << g.base << "+" << m_number_cols << " while cells <= %"
                << CSeqNo::seqoff(rcv.cell_base, rcv.cells.size()-1));
        return;
    }

    bool last = true;
    loss_seqs_t::value_type val;
    for (size_t i = offset; i < maxoff; ++i)
    {
        bool gone = last;
        last = rcv.cells[i];

        if (gone && !last)
        {
            // Switch full -> loss. Store the sequence, as single (for now)
            val.first = val.second = CSeqNo::incseq(base, i);
        }
        else if (last && !gone)
        {
            val.second = CSeqNo::incseq(base, i);
            irrecover.push_back(val);
        }
    }

    // If it happened that 0 cells were until the end, we are
    // sure that we have the val.first set to the first of the loss list
    // and we've reached the end. Otherwise 'last' would be true.
    if (!last)
    {
        val.second = CSeqNo::incseq(base, int(maxoff)-1);
        irrecover.push_back(val);
    }

    g.dismissed = true;
}

static inline char CellMark(const std::deque<bool>& cells, int index)
{
    if (index >= int(cells.size()))
        return '/';

    return cells[index] ? '#' : '.';
}

#if ENABLE_HEAVY_LOGGING
static void DebugPrintCells(int32_t base, const std::deque<bool>& cells, int row_size)
{
    int i = 0;
    // Shift to the first empty cell
    for ( ; i < int(cells.size()); ++i)
        if (cells[i] == false)
            break;

    if (i == int(cells.size()))
    {
        LOGC(mglog.Debug, log << "FEC: ... cell[0-" << (cells.size()-1) << "]: ALL CELLS EXIST");
        return;
    }

    // Ok, we have some empty cells, so just adjust to the start of a row.
    i -= i % row_size;
    if (i < 0)
        i = 0; // you never know...

    for ( ; i < int(cells.size()); i += row_size )
    {
        std::ostringstream os;
        os << "cell[" << i << "-" << (i+row_size-1) << "] %" << CSeqNo::incseq(base, i) << ":";
        for (int y = 0; y < row_size; ++y)
        {
            os << " " << CellMark(cells, i+y);
        }
        LOGP(mglog.Debug, os.str());
    }
}
#else
static void DebugPrintCells(int32_t /*base*/, const std::deque<bool>& /*cells*/, int /*row_size*/) {}
#endif

bool FECFilterBuiltin::HangHorizontal(const CPacket& rpkt, bool isfec, loss_seqs_t& irrecover)
{
    int32_t seq = rpkt.getSeqNo();

    int rowx = RcvGetRowGroupIndex(seq);
    if (rowx == -1)
        return false;

    RcvGroup& rowg = rcv.rowq[rowx];
    // Clip the packet into the horizontal group.

    // If this was a regular packet, increase the number of collected.
    // If this was a FEC/CTL packet, keep this number, just set the fec flag.
    if (isfec)
    {
        if (!rowg.fec)
        {
            ClipControlPacket(rowg, rpkt);
            rowg.fec = true;
            HLOGC(mglog.Debug, log << "FEC/H: FEC/CTL packet clipped, %" << seq << " base=%" << rowg.base);
        }
        else
        {
            HLOGC(mglog.Debug, log << "FEC/H: FEC/CTL at %" << seq << " DUPLICATED, skipping.");
        }
    }
    else
    {
        ClipPacket(rowg, rpkt);
        rowg.collected++;
        HLOGC(mglog.Debug, log << "FEC/H: DATA packet clipped, %" << seq
                << ", received " << rowg.collected << "/" << sizeRow()
                << " base=%" << rowg.base);
    }

    if (rowg.fec && rowg.collected == m_number_cols - 1)
    {
        HLOGC(mglog.Debug, log << "FEC/H: HAVE " << rowg.collected << " collected & FEC; REBUILDING...");
        // The group will provide the information for rebuilding.
        // The sequence of the lost packet can be checked in cells.
        // With the condition of 'collected == m_number_cols - 1', there
        // should be only one lacking packet, so just rely on first found.
        RcvRebuild(rowg, RcvGetLossSeqHoriz(rowg),
                m_number_rows == 1 ? Group::SINGLE : Group::HORIZ);

#if ENABLE_HEAVY_LOGGING
        std::ostringstream os;
        for (size_t i = 0; i < rcv.rebuilt.size(); ++i)
        {
            os << " " << rcv.rebuilt[i].hdr[SRT_PH_SEQNO];
        }

        LOGC(mglog.Debug, log << "FEC: ... cached rebuilt packets (" << rcv.rebuilt.size() << "):" << os.str());
#endif
    }

    // When there are only rows, dismiss the oldest row when you have
    // collected at least 1 packet in the next group. Do not dismiss
    // any groups here otherwise - all will be decided during column
    // processing.

    bool want_collect_irrecover = false;
    bool want_remove_cells = false;

    if (rcv.rowq.size() > 1)
    {
        if (m_number_rows == 1)
        {
            want_remove_cells = true;
            want_collect_irrecover = true;
        }
        else if (m_fallback_level == SRT_ARQ_ONREQ)
        {
            want_collect_irrecover = true;
        }
    }

    if (want_collect_irrecover)
    {
        int current = rcv.rowq.size() - 2;
        // We know we have at least 2 rows.
        // This value is then 0 or more.
        int past = current - 1;

        // To trigger irrecoverable collection, the current sequence
        // must be further than 1/3 of the row size to start from
        // the previous row. Otherwise, start with the past-previous
        // one, as long as it still exists.

        bool early SRT_ATR_UNUSED = false;
        if (past > 0)
        {
            // If you already have at least 3 rows, sweep starting from
            // the before-previous one (this will become 0 when the number
            // of rows is exactly 3).
            --past;
        }
        else
        {
            // If you have 2 rows, then in the current row (1) there must
            // be the sequence passing already the 1/3 of the size. Otherwise
            // decrease past to make it -1 and not pass the next test.
            if (CSeqNo::seqoff(rcv.rowq[1].base, seq) <= int(m_number_cols/3))
            {
                --past;
            }
            else
            {
                early = true;
            }
        }

        if (past >= 0)
        {
            // Collect irrecoverable since the 'past' index up to 0.
            // If want_remove_cells, also remove these rows and corresponding cells.

            int nrowremove = 1 + past;
            HLOGC(mglog.Debug, log << "Collecting irrecoverable packets from " << nrowremove << " ROWS per offset "
                    << CSeqNo::seqoff(rcv.rowq[1].base, seq) << " vs. " << m_number_cols << "/3");

            for (int i = 0; i <= past; ++i)
            {
                CollectIrrecoverRow(rcv.rowq[i], irrecover);
            }

            if (want_remove_cells)
            {
                size_t npktremove = sizeRow() * nrowremove;
                size_t ersize = min(npktremove, rcv.cells.size());

                HLOGC(mglog.Debug, log << "FEC/H: Dismissing rows n=" << nrowremove
                        << ", starting at %" << rcv.rowq[0].base
                        << " AND " << npktremove << " CELLS, base switch %"
                        << rcv.cell_base << " -> %" << rcv.rowq[past].base);

                rcv.rowq.erase(rcv.rowq.begin(), rcv.rowq.begin() + 1 + past);
                rcv.cells.erase(rcv.cells.begin(), rcv.cells.begin() + ersize);

                // We state that we have removed as many cells as for the removed
                // rows. In case when the number of cells proved to be less than that,
                // it will simply remove all cells. So now set the cell base to be
                // in sync with the row base.
                rcv.cell_base = rcv.rowq[0].base;
                DebugPrintCells(rcv.cell_base, rcv.cells, sizeRow());
            }
        }
        else
        {
            HLOGC(mglog.Debug, log << "FEC: NOT collecting irrecover from rows: distance="
                    << CSeqNo::seqoff(rcv.rowq[0].base, seq));
        }

    }

    return true;
}

int32_t FECFilterBuiltin::RcvGetLossSeqHoriz(Group& g)
{
    int baseoff = CSeqNo::seqoff(rcv.cell_base, g.base);
    if (baseoff < 0)
    {
        LOGC(mglog.Error, log << "FEC: IPE: negative cell offset, cell_base=%" << rcv.cell_base << " Group's base: %" << g.base << " - NOT ATTEMPTING TO REBUILD");
        return -1;
    }

    // This is a row, so start from the first cell for this group
    // and search lineraly for the first loss.

    int offset = -1;

    for (size_t cix = baseoff; cix < baseoff + m_number_cols; ++cix)
    {
        if (!rcv.CellAt(cix))
        {
            offset = cix;
#if ENABLE_HEAVY_LOGGING
            // For heavy logging case, show all cells in the range
            LOGC(mglog.Debug, log << "FEC/H: cell %" << CSeqNo::incseq(rcv.cell_base, cix)
                    << " (+" << cix << "): MISSING");

#else

            // Find just one. No more that just one shall be found
            // because it was checked earlier that we have collected
            // all but just one packet.
            break;
#endif
        }
#if ENABLE_HEAVY_LOGGING
        else
        {
            LOGC(mglog.Debug, log << "FEC/H: cell %" << CSeqNo::incseq(rcv.cell_base, cix)
                    << " (+" << cix << "): exists");
        }
#endif
    }

    if (offset == -1)
    {
        LOGC(mglog.Fatal, log << "FEC/H: IPE: rebuilding attempt, but no lost packet found");
        return -1; // sanity, shouldn't happen
    }

    // Now that we have an offset towards the first packet in the cells,
    // translate it to the sequence number of the lost packet.
    return CSeqNo::incseq(rcv.cell_base, offset);
}

int32_t FECFilterBuiltin::RcvGetLossSeqVert(Group& g)
{
    int baseoff = CSeqNo::seqoff(rcv.cell_base, g.base);
    if (baseoff < 0)
    {
        LOGC(mglog.Error, log << "FEC: IPE: negative cell offset, cell_base=%" << rcv.cell_base << " Group's base: %" << g.base << " - NOT ATTEMPTING TO REBUILD");
        return -1;
    }

    // This is a row, so start from the first cell for this group
    // and search lineraly for the first loss.

    int offset = -1;

    for (size_t col = 0; col < sizeCol(); ++col)
    {
        size_t cix = baseoff + (col * sizeRow());
        if (!rcv.CellAt(cix))
        {
            offset = cix;
#if ENABLE_HEAVY_LOGGING
            // For heavy logging case, show all cells in the range
            LOGC(mglog.Debug, log << "FEC/V: cell %" << CSeqNo::incseq(rcv.cell_base, cix)
                    << " (+" << cix << "): MISSING");

#else

            // Find just one. No more that just one shall be found
            // because it was checked earlier that we have collected
            // all but just one packet.
            break;
#endif
        }
#if ENABLE_HEAVY_LOGGING
        else
        {
            LOGC(mglog.Debug, log << "FEC/V: cell %" << CSeqNo::incseq(rcv.cell_base, cix)
                    << " (+" << cix << "): exists");
        }
#endif
    }

    if (offset == -1)
    {
        LOGC(mglog.Fatal, log << "FEC/V: IPE: rebuilding attempt, but no lost packet found");
        return -1; // sanity, shouldn't happen
    }

    // Now that we have an offset towards the first packet in the cells,
    // translate it to the sequence number of the lost packet.
    return CSeqNo::incseq(rcv.cell_base, offset);
}

void FECFilterBuiltin::RcvRebuild(Group& g, int32_t seqno, Group::Type tp)
{
    if (seqno == -1)
        return;

    uint16_t length_hw = ntohs(g.length_clip);
    if (length_hw > payloadSize())
    {
        LOGC(mglog.Error, log << "FEC: DECLIPPED length '" << length_hw << "' exceeds payload size. NOT REBUILDING.");
        return;
    }

    // Rebuild the packet
    // (length_hw is automatically converted through PrivPacket constructor)
    rcv.rebuilt.push_back( length_hw );

    Receive::PrivPacket& p = rcv.rebuilt.back();

    p.hdr[SRT_PH_SEQNO] = seqno;

    // This is for live mode only, for now, so the message
    // number will be always 1, PB_SOLO, INORDER, and flags from clip.
    // The REXMIT flag is set to 1 to fake that the packet was
    // retransmitted. It is necessary because this packet will
    // come out of sequence order, and if such a packet has
    // no rexmit flag set, it's treated as reordered by network,
    // which isn't true here.
    p.hdr[SRT_PH_MSGNO] = 1
        | MSGNO_PACKET_BOUNDARY::wrap(PB_SOLO)
        | MSGNO_PACKET_INORDER::wrap(rcv.order_required)
        | MSGNO_ENCKEYSPEC::wrap(g.flag_clip)
        | MSGNO_REXMIT::wrap(true)
        ;

    p.hdr[SRT_PH_TIMESTAMP] = g.timestamp_clip;
    p.hdr[SRT_PH_ID] = rcv.id;

    // Header ready, now we rebuild the contents
    // First, rebuild the length.

    // Allocate the buffer and assign to a packet.
    // This is only temporary, it will be copied to
    // the target place when needed, with the buffer coming
    // from the unit queue.

    // The payload clip may be longer than length_hw, but it
    // contains only trailing zeros for completion, which are skipped.
    copy(g.payload_clip.begin(), g.payload_clip.end(), p.buffer);

    HLOGC(mglog.Debug, log << "FEC: REBUILT: %" << seqno
            << " msgno=" << MSGNO_SEQ::unwrap(p.hdr[SRT_PH_MSGNO])
            << " flags=" << PacketMessageFlagStr(p.hdr[SRT_PH_MSGNO])
            << " TS=" << p.hdr[SRT_PH_TIMESTAMP] << " ID=" << dec << p.hdr[SRT_PH_ID]
            << " size=" << length_hw
            << " !" << BufferStamp(p.buffer, p.length));

    // If this is a single request (filled from row and m_number_cols == 1),
    // do not attempt recursive rebuilding
    if (tp == Group::SINGLE)
        return;

    // Mark this packet received
    MarkCellReceived(seqno);

    // This flips HORIZ/VERT
    Group::Type crosstype = Group::Type(!tp);

    if (crosstype == Group::HORIZ)
    {
        // Find this packet in the horizontal group
        int rowx = RcvGetRowGroupIndex(seqno);
        if (rowx == -1)
            return; // can't access any group to rebuild
        RcvGroup& rowg = rcv.rowq[rowx];

        // Sanity check. It's impossible that the packet was already
        // rebuilt and any attempt to rebuild a lacking packet was made.
        if (rowg.collected > m_number_cols - 1)
        {
            return;
        }

        // Same as ClipPacket for the incoming packet, just this
        // is extracting the data directly from the rebuilt one.
        ClipRebuiltPacket(rowg, p);
        rowg.collected++;
        HLOGC(mglog.Debug, log << "FEC/H: REBUILT packet clipped, %" << seqno
                << ", received " << rowg.collected << "/" << m_number_cols
                << " FOR base=%" << rowg.base);

        // Similar as by HangHorizontal, just don't collect irrecoverable packets.
        // They are already known when the packets were collected.
        if (rowg.fec && rowg.collected == m_number_cols - 1)
        {
            HLOGC(mglog.Debug, log << "FEC/H: with FEC-rebuilt HAVE " << rowg.collected << " collected & FEC; REBUILDING");
            // The group will provide the information for rebuilding.
            // The sequence of the lost packet can be checked in cells.
            // With the condition of 'collected == m_number_cols - 1', there
            // should be only one lacking packet, so just rely on first found.

            // NOTE: RECURSIVE CALL.
            RcvRebuild(rowg, RcvGetLossSeqHoriz(rowg), crosstype);
        }
    }
    else // crosstype == Group::VERT
    {
        // Find this packet in the vertical group
        int colx = RcvGetColumnGroupIndex(seqno);
        if (colx == -1)
            return; // can't access any group to rebuild
        RcvGroup& colg = rcv.colq[colx];

        // Sanity check. It's impossible that the packet was already
        // rebuilt and any attempt to rebuild a lacking packet was made.
        if (colg.collected > m_number_rows - 1)
        {
            return;
        }

        // Same as ClipPacket for the incoming packet, just this
        // is extracting the data directly from the rebuilt one.
        ClipRebuiltPacket(colg, p);
        colg.collected++;
        HLOGC(mglog.Debug, log << "FEC/V: REBUILT packet clipped, %" << seqno
                << ", received " << colg.collected << "/" << m_number_rows
                << " FOR base=%" << colg.base);

        // Similar as by HangVertical, just don't collect irrecoverable packets.
        // They are already known when the packets were collected.
        if (colg.fec && colg.collected == m_number_rows - 1)
        {
            HLOGC(mglog.Debug, log << "FEC/V: with FEC-rebuilt HAVE " << colg.collected << " collected & FEC; REBUILDING");
            // The group will provide the information for rebuilding.
            // The sequence of the lost packet can be checked in cells.
            // With the condition of 'collected == m_number_rows - 1', there
            // should be only one lacking packet, so just rely on first found.

            // NOTE: RECURSIVE CALL.
            RcvRebuild(colg, RcvGetLossSeqVert(colg), crosstype);
        }
    }

}

int FECFilterBuiltin::ExtendRows(int rowx)
{
    // Check if oversize. Oversize is when the
    // index is > 2*m_number_cols. If so, shrink
    // the container first.

#if ENABLE_HEAVY_LOGGING
    LOGC(mglog.Debug, log << "FEC: ROW STATS BEFORE: n=" << rcv.rowq.size());

    for (size_t i = 0; i < rcv.rowq.size(); ++i)
        LOGC(mglog.Debug, log << "... [" << i << "] " << rcv.rowq[i].DisplayStats());
#endif

    if (rowx > int(m_number_cols*3))
    {
        LOGC(mglog.Error, log << "FEC/H: OFFSET=" << rowx << " exceeds maximum row container size, SHRINKING rows and cells");

        rcv.rowq.erase(rcv.rowq.begin(), rcv.rowq.begin() + m_number_cols);
        rowx -= m_number_cols;

        // With rows, delete also an appropriate number of cells.
        int nerase = min(int(rcv.cells.size()), CSeqNo::seqoff(rcv.cell_base, rcv.rowq[0].base));
        rcv.cells.erase(rcv.cells.begin(), rcv.cells.begin() + nerase);
        rcv.cell_base = rcv.rowq[0].base;
    }

    // Create and configure next groups.
    size_t old = rcv.rowq.size();

    // First, add the number of groups.
    rcv.rowq.resize(rowx + 1);

    // Starting from old size
    for (size_t i = old; i < rcv.rowq.size(); ++i)
    {
        // Initialize the base for the row group
        int32_t ibase = CSeqNo::incseq(rcv.rowq[0].base, i*m_number_cols);
        ConfigureGroup(rcv.rowq[i], ibase, 1, m_number_cols);
    }

#if ENABLE_HEAVY_LOGGING
    LOGC(mglog.Debug, log << "FEC: ROW STATS AFTER: n=" << rcv.rowq.size());

    for (size_t i = 0; i < rcv.rowq.size(); ++i)
        LOGC(mglog.Debug, log << "... [" << i << "] " << rcv.rowq[i].DisplayStats());
#endif

    return rowx;
}

int FECFilterBuiltin::RcvGetRowGroupIndex(int32_t seq)
{
    RcvGroup& head = rcv.rowq[0];
    int32_t base = head.base;

    int offset = CSeqNo::seqoff(base, seq);

    // Discard the packet, if older than base.
    if (offset < 0)
    {
        HLOGC(mglog.Debug, log << "FEC/H: Packet %" << seq << " is in the past, ignoring");
        return -1;
    }

    // Hang in the receiver group first.
    size_t rowx = offset / m_number_cols;

    /*
       Don't.
       Leaving this code for future if needed, but this check should not be done.
       The resource management for "crazy" sequence numbers is done in the beginning,
       so simply TRUST THIS SEQUENCE, no matter what. After the check it won't do any harm.
       if (rowx > numberRows()*2) // past twice the matrix
       {
       LOGC(mglog.Error, log << "FEC/H: Packet %" << seq << " is in the far future, ignoring");
       return -1;
       }
     */

    // The packet might have come completely out of the blue.
    // The row group container must be prepared to extend
    // itself in order to give place for the packet.

    // First, possibly extend the row container
    if (rowx >= rcv.rowq.size())
    {
        rowx = ExtendRows(rowx);
    }

    return rowx;
}

void FECFilterBuiltin::MarkCellReceived(int32_t seq)
{
    // Mark the packet as received. This will allow later to
    // determine, which exactly packet is lost and needs rebuilding.
    int cellsize = rcv.cells.size();
    int cell_offset = CSeqNo::seqoff(rcv.cell_base, seq);
    bool resized SRT_ATR_UNUSED = false;
    if (cell_offset >= cellsize)
    {
        // Expand the cell container with zeros, excluding the 'cell_offset'.
        // Resize normally up to the required size, just set the lastmost
        // item to true.
        resized = true;
        rcv.cells.resize(cell_offset+1, false);
    }
    rcv.cells[cell_offset] = true;

    HLOGC(mglog.Debug, log << "FEC: MARK CELL RECEIVED: %" << seq << " - cells base=%"
            << rcv.cell_base << "[" << cell_offset << "]+" << rcv.cells.size()
            << (resized ? "(resized)":"") << " :");

    DebugPrintCells(rcv.cell_base, rcv.cells, sizeRow());
}

bool FECFilterBuiltin::IsLost(int32_t seq) const
{
    int offset = CSeqNo::seqoff(rcv.cell_base, seq);
    if (offset < 0)
    {
        LOGC(mglog.Error, log << "FEC: IsLost: IPE: %" << seq
                << " is earlier than the cell base %" << rcv.cell_base);
        return true; // fake we have the packet - this is to collect losses only
    }
    if (offset >= int(rcv.cells.size()))
    {
        // XXX IPE!
        LOGC(mglog.Error, log << "FEC: IsLost: IPE: %" << seq << " is past the cells %"
                << rcv.cell_base << " + " << rcv.cells.size());
        return true;
    }

    return rcv.cells[offset];
}

bool FECFilterBuiltin::HangVertical(const CPacket& rpkt, signed char fec_col, loss_seqs_t& irrecover)
{
    bool fec_ctl = (fec_col != -1);
    // Now hang the packet in the vertical group

    int32_t seq = rpkt.getSeqNo();

    // Ok, now we have the column index, we know it exists.
    // Apply the packet.

    int colgx = RcvGetColumnGroupIndex(seq);
    if (colgx == -1)
        return false;

    RcvGroup& colg = rcv.colq[colgx];

    if (fec_ctl)
    {
        if (!colg.fec)
        {
            ClipControlPacket(colg, rpkt);
            colg.fec = true;
            HLOGC(mglog.Debug, log << "FEC/V: FEC/CTL packet clipped, %" << seq << " FOR COLUMN " << int(fec_col)
                    << " base=%" << colg.base);
        }
        else
        {
            HLOGC(mglog.Debug, log << "FEC/V: FEC/CTL at %" << seq << " COLUMN " << int(fec_col) << " DUPLICATED, skipping.");
        }
    }
    else
    {
        // Data packet, clip it as data
        ClipPacket(colg, rpkt);
        colg.collected++;
        HLOGC(mglog.Debug, log << "FEC/V: DATA packet clipped, %" << seq
                << ", received " << colg.collected << "/" << sizeCol()
                << " base=%" << colg.base);
    }

    if (colg.fec && colg.collected == m_number_rows - 1)
    {
        HLOGC(mglog.Debug, log << "FEC/V: HAVE " << colg.collected << " collected & FEC; REBUILDING");
        RcvRebuild(colg, RcvGetLossSeqVert(colg), Group::VERT);
    }

    // Column dismissal takes place under very strictly specified condition,
    // so simply call it in general here. At least it may happen potentially
    // at any time of when a packet has been received.
    RcvCheckDismissColumn(rpkt.getSeqNo(), colgx, irrecover);

#if ENABLE_HEAVY_LOGGING
    LOGC(mglog.Debug, log << "FEC: COL STATS ATM: n=" << rcv.colq.size());

    for (size_t i = 0; i < rcv.colq.size(); ++i)
        LOGC(mglog.Debug, log << "... [" << i << "] " << rcv.colq[i].DisplayStats());
#endif

    return true;
}

void FECFilterBuiltin::RcvCheckDismissColumn(int32_t seq, int colgx, loss_seqs_t& irrecover)
{
    // The first check we need to do is:
    //
    // - get the column number
    // - get the series for this column
    // - if series is 0, just return

    int series = colgx / numberCols();
    if (series == 0)
        return;

    //  - STARTING from the same column series 0:
    //  - unless DISMISSED, collect all irrecoverable packets from this group
    //  - mark this column DISMISSED
    //  - SAME CHECK for previous group, until index 0

    set<int32_t> loss;

    int colx SRT_ATR_UNUSED = colgx % numberCols();

    HLOGC(mglog.Debug, log << "FEC/V: going to DISMISS cols past %" << seq
            << " at INDEX=" << colgx << " col=" << colx
            << " series=" << series << " - looking up candidates...");

    // Walk through all column groups in series 0. Collect irrecov's
    // from every group, for which the incoming 'seq' is in future.
    for (size_t i = 0; i < numberCols(); ++i)
    {
        RcvGroup& pg = rcv.colq[i];
        if (pg.dismissed)
        {
            HLOGC(mglog.Debug, log << "FEC/V: ... [" << i << "] base=%"
                    << pg.base << " ALREADY DISMISSED, skipping.");
            continue;
        }

        // With multi-staircase it may happen that THIS column contains
        // sequences that are all in the past, but the PREVIOUS column
        // has some in the future, because THIS column is the top of
        // the second staircase, and PREVIOUS is the bottom stair of
        // the first staircase. When this is confirmed, simply skip
        // the columns that have the highest sequence in the future
        // because they can't be dismissed yet. Jump them over, so maybe
        // they can be dismissed in future.
        int this_col_offset = CSeqNo::seqoff(pg.base, seq);
        int last_seq_offset = this_col_offset - (sizeCol()-1)*sizeRow();

        if (last_seq_offset < 0)
        {
            HLOGC(mglog.Debug, log << "FEC/V: ... [" << i << "] base=%"
                    << pg.base << " TOO EARLY (last=%"
                    << CSeqNo::incseq(pg.base, (sizeCol()-1)*sizeRow())
                    << ")");
            continue;
        }

        // NOTE: If it was standing on the second staircase top, there's
        // still a chance that it hits the staircase top of the first
        // staircase and will dismiss it as well.

        HLOGC(mglog.Debug, log << "FEC/V: ... [" << i << "] base=%"
                << pg.base << " - PAST last=%"
                << CSeqNo::incseq(pg.base, (sizeCol()-1)*sizeRow())
                << " - collecting losses.");

        pg.dismissed = true; // mark irrecover already collected
        for (size_t sof = 0; sof < pg.step * sizeCol(); sof += pg.step)
        {
            int32_t lseq = CSeqNo::incseq(pg.base, sof);
            if (!IsLost(lseq))
            {
                loss.insert(lseq);
                HLOGC(mglog.Debug, log << "FEC: ... cell +" << sof << " %" << lseq
                        << " lost");
            }
            else
            {
                HLOGC(mglog.Debug, log << "FEC: ... cell +" << sof << " %" << lseq
                        << " EXISTS");
            }
        }
    }

    // COLUMN DISMISAL:

    // 1. We can only dismiss ONE SERIES OF COLUMNS - OR NOTHING.
    // 2. The triggering 'seq' must be past ANY sequence embraced
    // by any group in the first series of columns.

    // Useful information:
    //
    // 1. It's not known from upside, which column contains a sequence
    //    number that reaches FURTHEST. The safe statement is then:
    //    - For even arrangement, it must be past BASE0 + matrix size
    //    - For staircase arrangement - BASE0 + matrix size * 2.

    int32_t base0 = rcv.colq[0].base;
    int this_off = CSeqNo::seqoff(base0, seq);

    int mindist =
        m_arrangement_staircase ?
        (numberCols() * numberRows() * 2)
        :
        (numberCols() * numberRows());

    bool any_dismiss SRT_ATR_UNUSED = false;

    // if (base0 +% mindist) <% seq
    if (this_off < mindist)
    {
        HLOGC(mglog.Debug, log << "FEC/V: NOT dismissing any columns at %" << seq
                << ", need to pass %" << CSeqNo::incseq(base0, mindist));
    }
    else if (rcv.colq.size() < numberCols())
    {
        HLOGC(mglog.Debug, log << "FEC/V: IPE: about to dismiss past %" << seq
                << " with required %" << CSeqNo::incseq(base0, mindist)
                << " but col container size still " << rcv.colq.size());
    }
    else if (rcv.rowq.size() < numberRows())
    {
        HLOGC(mglog.Debug, log << "FEC/V: IPE: about to dismiss past %" << seq
                << " with required %" << CSeqNo::incseq(base0, mindist)
                << " but row container size still " << rcv.rowq.size());
    }
    else
    {
        // The condition for dismissal is now. The number of dismissed columns
        // is numberCols(), regardless of the required 'mindinst'.
        any_dismiss = true;

        int32_t newbase = rcv.colq[numberCols()].base;
        int32_t newbase_row = rcv.rowq[numberRows()].base;
        int matrix_size = numberCols() * numberRows();

        HLOGC(mglog.Debug, log << "FEC/V: DISMISSING " << numberCols() << " COLS. Base %"
                << rcv.colq[0].base << " -> %" << newbase
                << " AND " << numberRows() << " ROWS Base %"
                << rcv.rowq[0].base << " -> %" << newbase_row
                << " AND " << matrix_size << " cells");

        rcv.colq.erase(rcv.colq.begin(), rcv.colq.begin() + numberCols());

#if ENABLE_HEAVY_LOGGING
        LOGC(mglog.Debug, log << "FEC: COL STATS BEFORE: n=" << rcv.colq.size());

        for (size_t i = 0; i < rcv.colq.size(); ++i)
            LOGC(mglog.Debug, log << "... [" << i << "] " << rcv.colq[i].DisplayStats());
#endif

        // Now erase accordingly one matrix of rows.
        // Sanity check
        if (newbase_row != newbase)
        {
            LOGC(mglog.Fatal, log << "FEC/V: IPE: DISCREPANCY in base0 col=%"
                    << newbase << " row=%" << newbase_row << " - DELETING ALL ROWS");

            // Delete all rows and reinitialize them.
            rcv.rowq.clear();
            rcv.rowq.resize(1);
            ConfigureGroup(rcv.rowq[0], newbase, 1, sizeRow());
        }
        else
        {
            // Remove "legally" a matrix of rows.
            rcv.rowq.erase(rcv.rowq.begin(), rcv.rowq.begin() + numberRows());
        }

        // And now accordingly remove cells. Exactly one matrix of cells.
        // Sanity check first.
        int32_t newbase_cell = CSeqNo::incseq(rcv.cell_base, matrix_size);
        if (newbase != newbase_cell)
        {
            LOGC(mglog.Fatal, log << "FEC/V: IPE: DISCREPANCY in base0 col=%"
                    << newbase << " row=%" << newbase_row << " - DELETING ALL ROWS");

            // Try to shift it gently first. Find the cell that matches the base.
            int shift = CSeqNo::seqoff(rcv.cell_base, newbase);
            if (shift < 0)
                rcv.cells.clear();
            else
                rcv.cells.erase(rcv.cells.begin(), rcv.cells.begin() + shift);
        }
        else
        {
            rcv.cells.erase(rcv.cells.begin(), rcv.cells.begin() + matrix_size);
        }
        rcv.cell_base = newbase;
        DebugPrintCells(rcv.cell_base, rcv.cells, sizeRow());
    }

    /*
       OLD UNUSED CODE, leaving for historical reasons

    // - check the last sequence of last column in series 0
    // - if passed sequence number is earlier than this, just return
    // - now that seq is newer than the last in the last column,
    //    - dismiss whole series 0 column groups

    // First, index of the last column
    size_t lastx = numberCols()-1;
    if (lastx < rcv.colq.size())
    {
    int32_t lastbase = rcv.colq[lastx].base;

    // Compare the base sequence with the sequence that caused the update
    int dist = CSeqNo::seqoff(lastbase, seq);

    // Shift this distance by the distance between the first and last
    // sequence managed by a singled column. This counts (sizeCol()-1)*step.
    dist -= (sizeCol()-1) * rcv.colq[lastx].step;

    // Now, if this value is in the past (negative), it means that the
    // 'seq' number is covered by this group or any earlier group. If so,
    // do nothing. If this value is positive, it means that this
    // sequence is in future towards the group that is in the last
    // column of series 0. If so, whole series 0 may be now dismissed.

    // NOTE: we don't care if lost packets have been collected for
    // the groups being dismissed. They *SHOULD* be, just as a fallback
    // SRT - if needed - will simply send LOSSREPORT request for all
    // packets that are lossreported and all older ones.

    if (dist > 0 && rcv.colq.size() > numberCols() )
    {
    any_dismiss = true;
    int32_t newbase = rcv.colq[numberCols()].base;
    rcv.colq.erase(rcv.colq.begin(), rcv.colq.begin() + numberCols());

    // colgx is INVALIDATED after removal
    int newcolgx SRT_ATR_UNUSED = colgx - numberCols();

    // After a column series was dismissed, now dismiss also
    // the same number of rows.
    // Do some sanity checks first.

    size_t nrowrem = 0;
    int32_t oldrowbase = rcv.rowq[0].base; // before it gets deleted
    if (rcv.rowq.size() > numberRows())
    {
    int32_t newrowbase = rcv.rowq[numberRows()].base;
    if (newbase != newrowbase)
    {
    LOGC(mglog.Error, log << "FEC: IPE: ROW/COL base DISCREPANCY:  Looking up lineraly for the right row.");

    // Fallback implementation in order not to break everything
    for (size_t r = 0; r < rcv.rowq.size(); ++r)
    {
    if (CSeqNo::seqoff(newbase, rcv.rowq[r].base) >= 0)
    {
    rcv.rowq.erase(rcv.rowq.begin(), rcv.rowq.begin() + r);
    nrowrem = r;
    break;
    }
    }
    }
    else
    {
    rcv.rowq.erase(rcv.rowq.begin(), rcv.rowq.begin() + numberRows());
    nrowrem = numberRows();
    }
    }

    // If rows were removed, so remove also cells
    if (nrowrem > 0)
    {
        int32_t newbase = rcv.rowq[0].base;

        // This value SHOULD be == nrowrem * sizeRow(), but this
        // calculation is safe against bugs. Report them, if found, though.
        int nrem = CSeqNo::seqoff(rcv.cell_base, newbase);

        if (oldrowbase != rcv.cell_base)
        {
            LOGC(mglog.Error, log << "FEC: CELL/ROW base discrepancy, calculating and resynchronizing");
        }
        else
        {
            HLOGC(mglog.Debug, log << "FEC: will remove " << nrem << " cells, SHOULD BE = "
                    << (nrowrem * sizeRow()));
        }

        if (nrem > 0)
        {
            // Now collect losses from all rows about to be dismissed.
            for (int sof = 0; sof < nrem; sof++)
            {
                int32_t lseq = CSeqNo::incseq(rcv.cell_base, sof);
                if (!IsLost(lseq))
                    loss.insert(lseq);
            }

            HLOGC(mglog.Debug, log << "FEC: ERASING unused cells (" << nrem << "): %"
                    << rcv.cell_base << " - %" << newbase
                    << ", losses collected: " << Printable(loss));

            rcv.cells.erase(rcv.cells.begin(), rcv.cells.begin() + nrem);
            rcv.cell_base = newbase;

            DebugPrintCells(rcv.cell_base, rcv.cells, sizeRow());
        }
        else
        {
            HLOGC(mglog.Debug, log << "FEC: NOT ERASING cells, base %" << rcv.cell_base
                    << " vs row base %" << rcv.rowq[0].base);
        }
    }

    HLOGC(mglog.Debug, log << "FEC/V: updated g=" << colgx << " -> " << newcolgx << " %"
            << rcv.colq[newcolgx].base << ", DISMISS up to g=" << numberCols()
            << " base=%" << lastbase
            << " ROW=%" << rcv.rowq[0].base << "+" << nrowrem);

}
}

// */

// Now all collected lost packets translate into the range list format
TranslateLossRecords(loss, irrecover);

HLOGC(mglog.Debug, log << "FEC: ... COLLECTED IRRECOVER: " << Printable(loss) << (any_dismiss ? " CELLS DISMISSED" : " nothing dismissed"));
}

void FECFilterBuiltin::TranslateLossRecords(const set<int32_t>& loss, loss_seqs_t& irrecover)
{
    if (loss.empty())
        return;

    // size() >= 1 granted
    set<int32_t>::iterator i = loss.begin();

    int32_t fi_start = *i;
    int32_t fi_end = fi_start;
    ++i;
    for (; i != loss.end(); ++i)
    {
        int dist = CSeqNo::seqoff(fi_end, *i);
        if (dist == 1)
            ++fi_end;
        else
        {
            // Jumped over some sequences, cut the range.
            irrecover.push_back(make_pair(fi_start, fi_end));
            fi_start = fi_end = *i;
        }
    }

    // And ship the last one
    irrecover.push_back(make_pair(fi_start, fi_end));
}

int FECFilterBuiltin::RcvGetColumnGroupIndex(int32_t seqno)
{
    // The column is only the column, not yet
    // exactly the index of the column group in the container.
    // It's like:

    //   0   1   2   3   4
    //  [A]  '   '   '   '
    //  [A] [A]  '   '   '
    //  [A] [A] [A]  '   '
    //  [A] [A] [A] [A]  '++
    //  [A]+[A] [A] [A] [A]+
    //  [B] [A]+[A] [A] [A]+
    //  [B] [B] [A]+[A] [A]+
    //  [B] [B] [B] [A]+[A]+
    //  [B] [B] [B] [B] [A]++
    //  [B]+[B] [B] [B] [B]
    //      [B] [B] [B] [B]
    //          [B] [B] [B]
    //              [B] [B]
    //                  [B]
    //
    // The same groups laid out in the container:
    //
    // [A]0 [A]1 [A]2 [A]3 [A]4 [B]0 [B]1 [B]2 [B]3 [B]4

    // This means, vert_gx returns the number for the
    // logical column, but we only know this column,
    // not the exact group that is assigned to this
    // packet. That is, in the above picture, if the
    // vert_gx resulted in 3, we still don't know if
    // this is A3 or B3.
    //
    // To know it, we need to first take the base
    // sequence number for the very first column group
    // in the container. Every next group in this
    // container is shifted by the 'slip' factor,
    // which in this case is m_number_cols + 1. The base
    // sequence shifted by m_number_cols*m_number_rows
    // becomes the sequence number of the next
    // group in the same column.
    //
    // If a single column is dismissed, then the
    // column 1 will become column 0, but the column
    // 1 in the next series will also become column 0.
    // All due to that the very base sequence for
    // all groups will be the one in the first series
    // column 1, now 0.
    //
    // Therefore, once we have the column, let's extract
    // the column base sequence.
    // As we can't count on that packets will surely come to close a group
    // or just a particular sequence number will be received, we simply have
    // to check all past groups at the moment when this packet is received:
    //
    // 1. Take the sequence number and determine both the GROUP INDEX and the
    // COLUMN INDEX of this group.
    //
    // Important thing here is that the column group base is the base sequence
    // number of the very first group, which NEED NOT GO HAND IN HAND with the
    // row group sequence base. The rules are then:
    //
    // OFFSET = DISTANCE( colq[0].base TO seq )
    //
    // COLUMN_INDEX = OFFSET % m_number_cols
    //
    // COLUMN_BASE = colq[COLUMN_INDEX].base
    //
    // COLUMN_OFFSET = DISTANCE(COLUMN_BASE TO seq)
    //
    // COLUMN_SERIES = COLUMN_OFFSET / (m_number_cols * m_number_rows)
    //
    // GROUP_INDEX = COLUMN_INDEX + (COLUMN_SERIES * m_number_cols)

    int offset = CSeqNo::seqoff(rcv.colq[0].base, seqno);
    if (offset < 0)
    {
        HLOGC(mglog.Debug, log << "FEC/V: %" << seqno << " in the past of col ABSOLUTE base %" << rcv.colq[0].base);
        return -1;
    }

    if (offset > CSeqNo::m_iSeqNoTH/2)
    {
        LOGC(mglog.Error, log << "FEC/V: IPE/ATTACK: pkt %" << seqno << " has CRAZY OFFSET towards the base %" << rcv.colq[0].base);
        return -1;
    }

    int colx = offset % m_number_cols;
    int32_t colbase = rcv.colq[colx].base;
    int coloff = CSeqNo::seqoff(colbase, seqno);
    if (coloff < 0)
    {
        HLOGC(mglog.Debug, log << "FEC/V: %" << seqno << " in the past of col #" << colx << " base %" << colbase);
        // This means that this sequence number predates the earliest
        // sequence number supported by the very first column.
        return -1;
    }

    int colseries = coloff / (m_number_cols * m_number_rows);
    size_t colgx = colx + (colseries * m_number_cols);

    HLOGC(mglog.Debug, log << "FEC/V: Lookup group for %" << seqno << ": cg_base=%" << rcv.colq[0].base
            << " column=" << colx << " with base %" << colbase << ": SERIES=" << colseries
            << " INDEX:" << colgx);

    // Check oversize. Dismiss some earlier items if it exceeds the size.
    // before you extend the size enormously.
    if (colgx > m_number_rows * m_number_cols * 2)
    {
        // That's too much
        LOGC(mglog.Error, log << "FEC/V: IPE or ATTACK: offset " << colgx << " is too crazy, ABORTING lookup");
        return -1;
    }

    if (colgx >= rcv.colq.size())
    {
        colgx = ExtendColumns(colgx);
    }

    return colgx;

    //
    // Even though column groups are arranged in a "staircase", it only means
    // that DISTANCE(colq[0].base TO colq[1].base) == 1 + m_number_cols, not 1.
    // But still, this DISTANCE(...) % m_number_cols == 1. So:
    //
    // COLUMN_INDEX = GROUP_INDEX % m_number_cols
    //
    // What is special here is that, group base sequence numbers with m_number_cols == 5
    // is, for example, these column groups in order of how they are arranged in
    // the container have their [INDEX] BASE (stating the very first has base == 10):
    //
    // [0] 10, [1] 16, [2] 22, [3] 28, [4] 34, [5] 40, [6] 46
    //
    // Therefore, if we get the sequence number 51, then:
    //
    // OFFSET = 51 - 10 = 41
    //
    // COLUMN_INDEX = 41 % 5[m_number_cols] == 1
    //
    // COLUMN_BASE = colq[1].base == 16
    //
    // COLUMN_OFFSET = DISTANCE(colq[1].base TO 51) = 51 - 16 = 35
    //
    // COLUMN_SERIES = COLUMN_OFFSET / (m_number_cols * m_number_rows) 
    //               = 35 / (5*5) = 35/25 = 1
    //
    // GROUP_INDEX = COLUMN_INDEX + (COLUMN_SERIES * m_number_cols)
    //             = 1 + 1*5 = 6
    //
    // --> We have identified column group with index 6, this one
    // that is designated above as [6] 46. This column group collects
    // the following sequence numbers:
    // - 46
    // - 51 (the one we were searching)
    // - 56
    // - 61
    // - 66
    //
    // We have then column group with index 6. Now we go backward in the column
    // group container starting from the direct previous column up to either
    // the start of container or the size == m_number_cols (5), to see if the
    // group "is closed". So:
    //
    // END = min(m_number_cols, colq.size()) <--- should be 5 or less, if there's less in the container
    //
    // FOR EACH i = 1 TO END:
    //     g = colq[GROUP_INDEX - i]
    //     gs = SHIFT(seq, -i)
    //     gmax = SHIFT(g.base, m_number_cols * m_number_rows)
    //     IF ( gs %> gmax )
    //        DISMISS COLUMNS from 0 to GROUP_INDEX - i; break

}

int FECFilterBuiltin::ExtendColumns(int colgx)
{
    if (colgx > int(sizeRow() * 2))
    {
        // This shouldn't happen because columns should be dismissed
        // once the last row of the first series is closed.
        LOGC(mglog.Error, log << "FEC/V: OFFSET=" << colgx << " exceeds maximum col container size, SHRINKING container by " << sizeRow());

        // Delete one series of columns.
        int32_t oldbase SRT_ATR_UNUSED = rcv.colq[0].base;
        rcv.colq.erase(rcv.colq.begin(), rcv.colq.begin() + numberCols());
        colgx -= numberCols();
        int32_t newbase = rcv.colq[0].base;

        // Delete also appropriate number of rows for one series
        rcv.rowq.erase(rcv.rowq.begin(), rcv.rowq.begin() + numberRows());

        // Sanity-check if the resulting row absolute base is equal to column
        if (rcv.rowq[0].base != newbase)
        {
            LOGC(mglog.Error, log << "FEC/V: IPE: removal of " << numberRows()
                    << " rows ships no same seq: rowbase=%"
                    << rcv.rowq[0].base
                    << " colbase=%" << oldbase << " -> %" << newbase << " - RESETTING ROWS");

            // How much you need, depends on the columns.
            size_t nseries = rcv.colq.size() / numberCols() + 1;
            size_t needrows = nseries * numberRows();

            rcv.rowq.clear();
            rcv.rowq.resize(needrows);
            int32_t rowbase = newbase;
            for (size_t i = 0; i < rcv.rowq.size(); ++i)
            {
                ConfigureGroup(rcv.rowq[i], rowbase, 1, sizeRow());
                rowbase = CSeqNo::incseq(newbase, sizeRow());
            }
        }

        size_t ncellrem = CSeqNo::seqoff(rcv.cell_base, newbase);
        rcv.cells.erase(rcv.cells.begin(), rcv.cells.begin() + ncellrem);
        rcv.cell_base = newbase;

        // Note that after this shift, column groups that were
        // in particular column, remain in that column.
    }

#if ENABLE_HEAVY_LOGGING
    LOGC(mglog.Debug, log << "FEC: COL STATS BEFORE: n=" << rcv.colq.size());

    for (size_t i = 0; i < rcv.colq.size(); ++i)
        LOGC(mglog.Debug, log << "... [" << i << "] " << rcv.colq[i].DisplayStats());
#endif

    // First, obtain the "series" of columns, possibly fixed.
    int series = colgx / numberCols();

    // Now, the base of the series is the base increased by one matrix size.

    int32_t base = rcv.colq[0].base;

    // This is the base for series 0, but this procedure must be prepared
    // for that the series will not necessarily be 1, may be greater.
    // Extension requires to be done in order to achieve this very index
    // existing in the column, so you need to add whole series in loop
    // until the series covering this shift is created.

    // Check, up to which series the columns are initialized.
    // Start with the series that doesn't exist
    int old_series = rcv.colq.size() / numberCols();

    // Each iteration of this loop adds one series of columns.
    // One series count numberCols() columns.
    for (int s = old_series; s <= series; ++s)
    {
        // We start with the base in series 0, the calculation of the
        // sequence number must happen anew for each one anyway, so it
        // doesn't matter from which start point.

        // Every base sequence for a series of columns is the series 0
        // base increased by one matrix size times series number.
        // THIS REMAINS TRUE NO MATTER IF WE USE STRAIGNT OR STAIRCASE ARRANGEMENT.
        int32_t sbase = CSeqNo::incseq(base, (numberCols()*numberRows()) * s);
        HLOGC(mglog.Debug, log << "FEC/V: EXTENDING column groups series " << s
                << ", size " << rcv.colq.size() << " -> "
                << (rcv.colq.size() + numberCols())
                << ", base=%" << base << " -> %" << sbase);

        // Every call to this function extends the given container
        // by 'gsize' number and configures each so added column accordingly.
        ConfigureColumns(rcv.colq, sbase);
    }

#if ENABLE_HEAVY_LOGGING
    LOGC(mglog.Debug, log << "FEC: COL STATS BEFORE: n=" << rcv.colq.size());

    for (size_t i = 0; i < rcv.colq.size(); ++i)
        LOGC(mglog.Debug, log << "... [" << i << "] " << rcv.colq[i].DisplayStats());
#endif

    return colgx;
}

