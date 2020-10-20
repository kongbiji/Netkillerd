#include "socket.h"

extern GW_info gw_info;
extern ATTACKER_info attacker_info;

bool connect_sock(int * client_sock, int server_port)
{
    bool ret = true;
    struct sockaddr_in server_addr, client_addr;
    socklen_t addr_size = sizeof(client_addr);
    if ((*client_sock = socket(AF_INET, SOCK_STREAM, 0)) == -1) // make client socket
        exit(1);

    memset(&client_addr, 0x00, addr_size);

    memset(&server_addr, 0x00, sizeof(server_addr));

    server_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(server_port);

    if (connect(*client_sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) // connect server socket
    {
        ret = false;
    }

    getsockname(*client_sock, (struct sockaddr *)&client_addr, &addr_size); // get client port

    return ret;
}

bool send_data(int client_sock, char *data)
{
    char result[BUF_SIZE] = {0};
    unsigned int data_length = strlen(data);
    result[0] = (data_length >> 8) & 0xFF;
    result[1] = data_length & 0xFF;
    memcpy(result + 2, data, strlen(data));

    if (write(client_sock, result, strlen(data) + 2) <= 0)
    {
        return false;
    }
    return true;
}

bool recv_data(int client_sock, char *data)
{
    unsigned char buf[BUF_SIZE] = {0};
    char result[BUF_SIZE] = {0};
    unsigned int data_length = 0;

    if (read(client_sock, buf, 2) < 0)
    {
        return false;
    }

    data_length = (buf[0] << 8) + buf[1];

    if (read(client_sock, result, data_length) <= 0)
    {
        return false;
    }

    result[data_length] = '\0';

    memcpy(data, result, sizeof(result));

    return true;
}

void scan_pkt_check(uint32_t ip, int client_sock){
    int k = 0;
    char err[PCAP_ERRBUF_SIZE];
    pcap_t* handle = pcap_open_live(gw_info.iface_name, BUFSIZ, 1, 1000, err);
    struct pcap_pkthdr* header;
    const u_char *rep;
    ARP_Packet * pkt_ptr;
    char data[1024] = {0,};
    data[0] = '3';
    data[1] = '\t';
    while(1){ //check correct arp reply
        if(k == 6){
            break; // not found
        }
        int ret = pcap_next_ex(handle, &header, &rep);          
        if(ret == 0 || ret == -1){
            k++;
            continue;
        }
        pkt_ptr = (ARP_Packet *)rep;
        if((ntohs(pkt_ptr->eth.ether_type) == 0x0806) 
            && (pkt_ptr->arp.sender_ip == ip) && (ntohs(pkt_ptr->arp.opcode) == 2)){
            char str_mac[21];
            char str_ip[16] = {0, };

            int la = sprintf(str_mac, "%02X:%02X:%02X:%02X:%02X:%02X\t",pkt_ptr->arp.sender_mac[0],pkt_ptr->arp.sender_mac[1],pkt_ptr->arp.sender_mac[2],
            pkt_ptr->arp.sender_mac[3],pkt_ptr->arp.sender_mac[4],pkt_ptr->arp.sender_mac[5]);
            strcat(data, str_mac);
            
            sprintf(str_ip, "%d.%d.%d.%d\t", (ip)&0xFF, (ip>>8)&0xFF, (ip>>16)&0xFF, (ip>>24)&0xFF);
            strcat(data, str_ip);

            send_data(client_sock, data);
            break;
        }
                    
        k++;
    }
    pcap_close(handle);
}

void scan_pkt_send(int client_sock){

    uint32_t broad_ip = 0;
    uint8_t broad_mac[6];
    memset(broad_mac, 0xff, sizeof(uint8_t)*6);

    char err[PCAP_ERRBUF_SIZE];
    pcap_t* handle = pcap_open_live(gw_info.iface_name, BUFSIZ, 1, 1000, err);
    struct pcap_pkthdr* header;
    const u_char *rep;
    ARP_Packet * arp_pkt = (ARP_Packet *)malloc(sizeof(ARP_Packet));
    // scanning
    for(int i = 1; i < 256; i++){
        uint32_t tmp_target_ip = gw_info.ip + htonl(i);

        memset(arp_pkt, 0, sizeof(ARP_Packet));
        u_char pkt[sizeof(ARP_Packet)];
        memset(pkt, 0, sizeof(ARP_Packet));

        make_arp_packet(broad_mac, attacker_info.mac, 0x1,
                                            attacker_info.ip, tmp_target_ip, arp_pkt);
        memcpy(pkt, arp_pkt, sizeof(ARP_Packet));

        // send arp req to find taregt mac
        if(pcap_sendpacket(handle, pkt ,sizeof(pkt))!=0){
            printf("[-] Error in find target's MAC\n");
            pcap_close(handle);
            exit(0);
        }
        std::thread scan_thread(scan_pkt_check, tmp_target_ip, client_sock);
        scan_thread.detach();   
        usleep( 1000 * 300 );
    }
                
    free(arp_pkt);
}