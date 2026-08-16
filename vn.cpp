#include <iostream>
#include <string>
#include <vector>
#include <cstring>
#include <cstdlib>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <map>
#include <set>
#include <cstdint>

using namespace std;

#pragma pack(push, 1)

struct HandshakePacket {
    uint32_t ip_address;
    uint16_t udp_port;   
};

struct LinkStateEntry {
    uint32_t neighbor_id;
    uint32_t ip_address;
    uint16_t udp_port;
    uint32_t cost;
};

struct LsaPacket {
    uint32_t originator_id; 
    uint32_t sequence_num;  
    uint32_t neighbor_id;
    uint32_t cost;          
};

#pragma pack(pop)

int main(int argc, char* argv[]) {
    if (argc != 5) {
        cerr << "Usage: " << argv[0] << " <ON_TCP_PORT> <IP_ON> <UDP_PORT> <MY_IP>\n";
        return 1;
    }

    int ON_TCP_PORT = atoi(argv[1]);
    string IP_ON = argv[2];
    int UDP_PORT = atoi(argv[3]);
    string MY_IP = argv[4];

    int tcp_sockfd = socket(AF_INET, SOCK_STREAM, 0);
    int udp_sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (tcp_sockfd < 0 || udp_sockfd < 0) {
        perror("Socket creation failed");
        return 1;
    }

    struct sockaddr_in my_udp_addr;
    memset(&my_udp_addr, 0, sizeof(my_udp_addr));
    my_udp_addr.sin_family = AF_INET;
    my_udp_addr.sin_port = htons(UDP_PORT);
    my_udp_addr.sin_addr.s_addr = inet_addr(MY_IP.c_str());
    if (bind(udp_sockfd, (struct sockaddr*)&my_udp_addr, sizeof(my_udp_addr)) < 0) {
        perror("UDP bind failed");
        return 1;
    }

    struct sockaddr_in on_addr;
    memset(&on_addr, 0, sizeof(on_addr));
    on_addr.sin_family = AF_INET;
    on_addr.sin_port = htons(ON_TCP_PORT);
    on_addr.sin_addr.s_addr = inet_addr(IP_ON.c_str());

    if (connect(tcp_sockfd, (struct sockaddr*)&on_addr, sizeof(on_addr)) < 0) {
        perror("TCP connection to ON failed");
        return 1;
    }
    cout << "Connected to ON." << endl;

    HandshakePacket handshake;
    handshake.ip_address = inet_addr(MY_IP.c_str());
    handshake.udp_port = htons(UDP_PORT);

    if (send(tcp_sockfd, &handshake, sizeof(handshake), 0) < 0) {
        perror("Failed to send handshake");
        return 1;
    }
    cout << "Sent handshake to ON." << endl;

    fd_set master_fds;
    int fdmax = max(tcp_sockfd, udp_sockfd);
    
    int my_node_id = -1;
    uint32_t my_sequence_num = 0;
    map<int, sockaddr_in> neighbor_addrs;
    map<uint32_t,uint32_t>latest_sequence_nums;
    vector<uint32_t>neighbours;
    map<int,map<int,int>>dist_pair;
    int expected_num_node=0;
    set<int> all_nodes;
    bool can_print_adj_matrix = false;
    while (true) {
        FD_ZERO(&master_fds);
        if (tcp_sockfd != -1) FD_SET(tcp_sockfd, &master_fds);
        FD_SET(udp_sockfd, &master_fds);
        struct timeval tv = {5, 0};
        select(fdmax + 1, &master_fds, NULL, NULL, &tv);
        int case_used = 0;

        if (tcp_sockfd != -1 && FD_ISSET(tcp_sockfd, &master_fds)) {
            vector<LinkStateEntry> received_entries(50);
            ssize_t nbytes = recv(tcp_sockfd, received_entries.data(), received_entries.size() * sizeof(LinkStateEntry), 0);
            
            if (nbytes <= 0) {
                cout << "Connection to ON lost." << endl;
                close(tcp_sockfd);
                tcp_sockfd = -1;
                break;
            }
            
            int num_entries = nbytes / sizeof(LinkStateEntry);
            my_node_id = ntohl(received_entries[num_entries - 1].neighbor_id);
            all_nodes.insert(my_node_id);
            
            neighbor_addrs.clear();
            neighbours.clear();
            dist_pair.clear();
            my_sequence_num++;
            latest_sequence_nums[my_node_id] = my_sequence_num;
            dist_pair[my_node_id][my_node_id] = 0;
            cout << "My assigned Node ID is: " << char('A'+my_node_id) << endl;
            // cout<<"My neighbours "<<"\n";
            vector<LsaPacket> lsas_to_flood;
            for (int i = 0; i < num_entries-1; ++i) {
                sockaddr_in neigh_addr;
                neigh_addr.sin_family = AF_INET;
                neigh_addr.sin_port = received_entries[i].udp_port;
                neigh_addr.sin_addr.s_addr = received_entries[i].ip_address;
                neighbor_addrs[ntohl(received_entries[i].neighbor_id)] = neigh_addr;
                
                neighbours.push_back(ntohl(received_entries[i].neighbor_id));
                LsaPacket lsa;
                lsa.originator_id = htonl(my_node_id);
                lsa.sequence_num = htonl(my_sequence_num);
                lsa.neighbor_id = received_entries[i].neighbor_id;
                lsa.cost = received_entries[i].cost;
                lsas_to_flood.push_back(lsa);
            }
            
            size_t payload_size = lsas_to_flood.size() * sizeof(LsaPacket);
            for(int i=0;i<num_entries-1;i++){
                sendto(udp_sockfd,lsas_to_flood.data(),payload_size,0,(const struct sockaddr*)&neighbor_addrs[neighbours[i]],sizeof(neighbor_addrs[neighbours[i]]));
            }
            case_used +=1;
        }
        
        
        if (FD_ISSET(udp_sockfd, &master_fds)) {
            case_used+=1;
            vector<LsaPacket> received_lsa(50);
            ssize_t nbytes = recvfrom(udp_sockfd, received_lsa.data(), received_lsa.size()*sizeof(LsaPacket), 0, NULL, NULL);
            
            int num_entries = nbytes / sizeof(LsaPacket);
            
            if(num_entries>0){
                uint32_t originator,seq;
                originator = ntohl(received_lsa[0].originator_id);
                seq = ntohl(received_lsa[0].sequence_num);

                if(latest_sequence_nums[originator] >= seq){
                    continue;
                }
                all_nodes.insert(originator);
                latest_sequence_nums[originator] = seq;
                cout<<"Received from "<<char('A'+originator) <<" ";
                for(int i=0;i<num_entries;i++){
                    int neigh_id = ntohl(received_lsa[i].neighbor_id); 
                    int c = ntohl(received_lsa[i].cost);
                    cout<<char( neigh_id + 'A')<<":"<<c;
                    dist_pair[originator][neigh_id] = c;
                    dist_pair[neigh_id][originator] = c;
                    dist_pair[neigh_id][neigh_id] = 0;
                    all_nodes.insert(neigh_id);
                    dist_pair[originator][originator] = 0;
                    if(i+1!=num_entries){
                        cout<<",";
                    }
                }
                cout<<"\n";

                size_t payload_size = num_entries * sizeof(LsaPacket);
                for(int i=0;i<neighbours.size();i++){
                    while(sendto(udp_sockfd,received_lsa.data(),payload_size,0,(const sockaddr*)&neighbor_addrs[neighbours[i]],sizeof(neighbor_addrs[neighbours[i]]))<0){
                        cerr<<"send to udp packet failed\n";
                    }
                }
                can_print_adj_matrix = true;
            }
        }

        if(case_used == 0 && can_print_adj_matrix){
            can_print_adj_matrix = 0;
            for(int i=0;i<all_nodes.size();i++){
                for(int j=0;j<all_nodes.size();j++){
                    if(i==j)
                        dist_pair[i][j]=0;
                    if(dist_pair.find(i)==dist_pair.end() || dist_pair[i].find(j)==dist_pair[i].end()){
                        dist_pair[i][j] = -1;
                    }
                }
            }
            for(auto n1:dist_pair){
                cout<<char( n1.first + 'A')<<": ";
                for(auto n2:n1.second){
                    cout<<char(n2.first+'A')<<":"<<n2.second<<" ";
                }
                cout<<"\n";
            }
        }


    }

    if (tcp_sockfd != -1) close(tcp_sockfd);
    close(udp_sockfd);
    return 0;
}