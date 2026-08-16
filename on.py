import socket
import select
import struct
import sys
import os
import time

HANDSHAKE_FORMAT = '!IH'
HANDSHAKE_SIZE = struct.calcsize(HANDSHAKE_FORMAT)

LINK_STATE_ENTRY_FORMAT = '!IIHI'
LINK_STATE_ENTRY_SIZE = struct.calcsize(LINK_STATE_ENTRY_FORMAT)

def get_file_mod_time(file_path):
    try:
        return os.path.getmtime(file_path)
    except OSError:
        return -1

def read_file(file_path):
    try:
        with open(file_path, 'r') as f:
            return f.read()
    except IOError as e:
        print(f"Error: Could not open file: {file_path}", file=sys.stderr)
        return ""

def parse_config(config_content):
    graph = []
    for line in config_content.splitlines():
        line = line.strip()
        if not line or line.startswith('#'):
            continue
        try:
            row = [int(cost) for cost in line.split()]
            if row:
                graph.append(row)
        except ValueError:
            print(f"Warning: Skipping invalid line in config: {line}", file=sys.stderr)
    return graph


def build_adjacency_matrix(upper_triangle):
    if not upper_triangle:
        return []
    n = len(upper_triangle) + 1
    adj_matrix = [[-1] * n for _ in range(n)]

    for i in range(n - 1):
        for j in range(len(upper_triangle[i])):
            cost = upper_triangle[i][j]
            adj_matrix[i][i + j + 1] = cost
            adj_matrix[i + j + 1][i] = cost

    for i in range(n):
        adj_matrix[i][i] = 0

    return adj_matrix

def send_link_state_updates(num_nodes, adj_matrix, vn_info, client_sockets):
    print("\n--- Sending LINK-STATE updates to all nodes ---")
    for node_id in range(num_nodes):
        client_sock = client_sockets[node_id]
        if client_sock is None:
            continue

        update_payload = bytearray()
        
        for neighbor_id in range(num_nodes):
            cost = adj_matrix[node_id][neighbor_id]
            if cost > 0:
                neighbor_ip, neighbor_port = vn_info[neighbor_id]
                entry = struct.pack(LINK_STATE_ENTRY_FORMAT, neighbor_id, neighbor_ip, neighbor_port, cost)
                update_payload.extend(entry)

        self_ip, self_port = vn_info[node_id]
        self_entry = struct.pack(LINK_STATE_ENTRY_FORMAT, node_id, self_ip, self_port, 0)
        update_payload.extend(self_entry)
        
        num_entries = len(update_payload) // LINK_STATE_ENTRY_SIZE
        print(f"Sending {num_entries} entries to Node {node_id}")
        try:
            client_sock.sendall(update_payload)
        except socket.error as e:
            print(f"Failed to send update to node {node_id}: {e}", file=sys.stderr)
            
    print("--- Finished sending updates ---\n")


def main():
    if len(sys.argv) != 2:
        print(f"Usage: {sys.argv[0]} <config_file>", file=sys.stderr)
        return 1

    config_path = sys.argv[1]

    config_content = read_file(config_path)
    if not config_content:
        return 1
        
    upper_triangle = parse_config(config_content)
    adj_matrix = build_adjacency_matrix(upper_triangle)
    num_nodes = len(adj_matrix)

    if num_nodes == 0:
        print("Error: Configuration file is empty or invalid. Exiting.", file=sys.stderr)
        return 1

    print(f"Adjacency Matrix ({num_nodes} nodes):")
    for row in adj_matrix:
        print(' '.join(map(str, row)))

    port = 5000
    server_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    server_socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    server_socket.setblocking(False)
    server_socket.bind(('', port))
    server_socket.listen(num_nodes)
    print(f"ON listening on port {port}...")

    inputs = [server_socket]
    clients_connected = 0
    vn_info = [None] * num_nodes
    client_sockets = [None] * num_nodes
    socket_to_node_id = {}
    last_mod_time = get_file_mod_time(config_path)

    periodic = None
    
    try:
        while True:
            readable, _, _ = select.select(inputs, [], [], 5.0)

            if periodic:
                periodic += 1
            current_mod_time = get_file_mod_time(config_path)
            if current_mod_time > last_mod_time or periodic==5:
                periodic = 1
                last_mod_time = current_mod_time
                config_content = read_file(config_path)
                upper_triangle = parse_config(config_content)
                adj_matrix = build_adjacency_matrix(upper_triangle)
                num_nodes_new = len(adj_matrix)
                
                if num_nodes_new != num_nodes:
                     print(f"Warning: Number of nodes changed from {num_nodes} to {num_nodes_new}. This may cause issues without a restart.", file=sys.stderr)
                     num_nodes = num_nodes_new

                print(f"Adjacency Matrix ({num_nodes} nodes) loaded.")

                if clients_connected == num_nodes:
                    send_link_state_updates(num_nodes, adj_matrix, vn_info, client_sockets)

            for s in readable:
                if s is server_socket:
                    if clients_connected < num_nodes:
                        conn, addr = s.accept()
                        print(f"Accepted new connection from {addr}")
                        conn.setblocking(False)
                        inputs.append(conn)
                    else:
                        print("Rejecting connection: already have max number of nodes.")
                        conn, addr = s.accept()
                        conn.close()

                else:
                    try:
                        data = s.recv(HANDSHAKE_SIZE)
                        if data:
                            if clients_connected < num_nodes and s not in socket_to_node_id:
                                ip_addr, udp_port = struct.unpack(HANDSHAKE_FORMAT, data)
                                
                                node_id = clients_connected
                                vn_info[node_id] = (ip_addr, udp_port)
                                client_sockets[node_id] = s
                                socket_to_node_id[s] = node_id
                                
                                clients_connected += 1
                                print(f"Received handshake from Node {node_id}. Total connected: {clients_connected}/{num_nodes}")

                                if clients_connected == num_nodes:
                                    print("\nAll nodes have reported in. Sending initial topology.")
                                    send_link_state_updates(num_nodes, adj_matrix, vn_info, client_sockets)
                                    periodic = 1
                        else:
                            print(f"Client {s.getpeername()} disconnected.")
                            inputs.remove(s)
                            s.close()
                            
                            
                    except socket.error as e:
                        print(f"Error on socket {s.getpeername()}: {e}")
                        inputs.remove(s)
                        s.close()

    except KeyboardInterrupt:
        print("\nServer shutting down.")
    finally:
        for s in inputs:
            s.close()
        
if __name__ == "__main__":
    main()
