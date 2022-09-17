-- For GB28181 TCP, RTP over RTP, see https://www.ietf.org/rfc/rfc4571.html
--
-- To apply this wireshark plugin:
--      mkdir -p ~/.local/lib/wireshark/plugins
--      ln -sf $(pwd)/gb28181.lua ~/.local/lib/wireshark/plugins/gb28181.lua

do
  -- RFC4517 RTP & RTCP over Connection-Oriented Transport
  local rtp_dissector = Dissector.get("rtp.rfc4571")

  local tcp_dissector_table = DissectorTable.get("tcp.port")
  tcp_dissector_table:add(9000, rtp_dissector)
end

