#!/usr/bin/python3

# This script is a testing tool that inserts a packet interceptor for
# a bond between two local ports and modify a packet that is sent over
# the UDP link used by the SRT connection.

# Provided by:
# https://github.com/FelixSodermanNeti

import socket, select, argparse

class UDPProxy:
    def __init__(self, caller_host, caller_port, listener_host, listener_port, break_at_pkt_NAK, break_at_pkt_ACK):

        # Listening socket configuration
        self.caller_host = caller_host
        self.caller_port = caller_port
        self.caller_socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.caller_socket.bind((self.caller_host, self.caller_port))

        # Forwarding destination configuration
        self.listener_host = listener_host
        self.listener_port = listener_port

        # Client address tracking
        self.client_address = None

        # Proxy state
        self.running = False

        # Break SRT
        self.sentPacketCounter = 0
        self.recivedAckCounter = 0
        self.PACKETBREAK_NAK = break_at_pkt_NAK
        self.PACKETBREAK_ACK = break_at_pkt_ACK

    def start(self):
        try:
            self.running = True
            print(
                f"UDP Proxy started. Listening on {self.caller_host}:{self.caller_port}. "
                f"Forwarding to {self.listener_host}:{self.listener_port}"
            )

            # Create forward socket
            listener_socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

            # Use select for non-blocking I/O
            while self.running:
                readable, _, _ = select.select([self.caller_socket, listener_socket], [], [], 1)

                for sock in readable:
                    if sock == self.caller_socket:
                        # Receive from client
                        data, addr = sock.recvfrom(65535)
                        self.client_address = addr

                        #Modify the NAK
                        processedData = data
                        # NAK
                        if(data[1] == 0x03 and len(data) == 24 and self.PACKETBREAK_NAK != 0): # IS A NAK, This should check for a 1 in the first byte as well.
                            print("Modifying NAK.")
                            newBytes = b"\xff\xff\xff\xff"
                            processedData =  data[:20] + newBytes + data[20 + len(newBytes):]
                            """
                            print("NAK BEFORE:")
                            print(string_bytearray_in_rows(data=data))
                            print("\n\nModifying NAK.\n\n")
                            print("ŃAK AFTER:")
                            print(string_bytearray_in_rows(data=processedData))
                            """

                        # ACK
                        if(data[1] == 0x02 and len(data) == 44): # IS A ACK
                            self.recivedAckCounter += 1
                            if (self.PACKETBREAK_ACK != 0 and self.recivedAckCounter == self.PACKETBREAK_ACK):
                                print("Modifying ACK.")
                                newBytes = b"\x7f\xff\xff\xff"
                                offset = 16
                                processedData =  data[:offset] + newBytes + data[offset + len(newBytes):]
                                """
                                print("ACK BEFORE:")
                                print(string_bytearray_in_rows(data=data))
                                print("\n\nModifying ACK.\n\n")
                                print("ACK AFTER:")
                                print(string_bytearray_in_rows(data=processedData))
                                """
                        listener_socket.sendto(
                            processedData,
                            (self.listener_host, self.listener_port)
                        )

                    elif sock == listener_socket:
                        # Receive from server
                        data, addr = sock.recvfrom(65535)
                        
                        # Drop a sequence of data packets (as to at least 3 to trigger an NAK with a range)
                        if len(data) == 1332: # Packet with data
                            self.sentPacketCounter += 1
                            if (self.PACKETBREAK_NAK != 0 and self.sentPacketCounter in range(self.PACKETBREAK_NAK, self.PACKETBREAK_NAK+5)):
                                print("Discarding packet.")
                                continue

                        if not self.client_address:
                            print("No client to send data to!")
                            continue

                        self.caller_socket.sendto(
                            data,
                            self.client_address
                        )

        except Exception as e:
            print(f"Proxy error: {e}")

        finally:
            self.stop()

    def stop(self):
        self.running = False
        self.caller_socket.close()
        print("UDP Proxy stopped.")


def string_bytearray_in_rows(data, bytes_per_row=4):
    tmpString = "\n"
    for i in range(0, len(data), bytes_per_row):
        row = data[i:i+bytes_per_row]
        tmpString += f"{i}: "
        tmpString += (" ".join(f"{byte:02x}" for byte in row))
        tmpString += "\n"
    tmpString += "\n"
    return tmpString

def main():
    # Parse command-line arguments
    parser = argparse.ArgumentParser(description='UDP Proxy')
    parser.add_argument('--caller-host', default='127.0.0.1',
                        help='Host to listen on (srt caller) (default: 127.0.0.1)')
    parser.add_argument('--caller-port', type=int, required=True,
                        help='Port to listen on (srt caller)')
    parser.add_argument('--listener-host', default='127.0.0.1',
                        help='Destination host to forward packets (srt listener)')
    parser.add_argument('--listener-port', type=int, required=True,
                        help='Destination port to forward packets (srt listener)')
    parser.add_argument('--break-at-pkt-NAK', type=int, default=0,
                        help='At what datapacket should the NAK -1 be sent (0=never)')
    parser.add_argument('--break-at-pkt-ACK', type=int, default=0,
                        help='At what datapacket should the ACK -1 be sent (0=never)')
    args = parser.parse_args()

    # Create and start proxy
    proxy = UDPProxy(
        args.caller_host,
        args.caller_port,
        args.listener_host,
        args.listener_port,
        args.break_at_pkt_NAK,
        args.break_at_pkt_ACK
    )

    try:
        proxy.start()
    except KeyboardInterrupt:
        proxy.stop()

if __name__ == '__main__':
    main()
