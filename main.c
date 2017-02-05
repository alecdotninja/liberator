#include <stdio.h>
#include <unistd.h>
#include <curl/curl.h>
#include <netinet/in.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <time.h>

extern char** environ;

const struct sockaddr_nl IPV4_ADDRESS_CHANGES = { AF_NETLINK, 0, 0, RTMGRP_IPV4_IFADDR };

int open_netlink_socket_for_ipv4_address_changes() {
    int netlink_socket;

    if ((netlink_socket = socket(PF_NETLINK, SOCK_RAW, NETLINK_ROUTE)) == -1) {
        return -1;
    }

    if (bind(netlink_socket, (struct sockaddr *)&IPV4_ADDRESS_CHANGES, sizeof(IPV4_ADDRESS_CHANGES)) == -1) {
        return -1;
    }

    return netlink_socket;
}

uint32_t wait_for_new_local_ipv4_address(int netlink_socket) {
    char message_buffer[4096];
    struct nlmsghdr *message = (struct nlmsghdr *)message_buffer;
    ssize_t message_length;

    while ((message_length = recv(netlink_socket, message, 4096, 0)) > 0) {
        while ((NLMSG_OK(message, message_length)) && (message->nlmsg_type != NLMSG_DONE)) {
            if (message->nlmsg_type == RTM_NEWADDR) {
                struct ifaddrmsg *ifa = (struct ifaddrmsg *)NLMSG_DATA(message);
                struct rtattr *rth = IFA_RTA(ifa);
                int rtl = IFA_PAYLOAD(message);

                while (rtl && RTA_OK(rth, rtl)) {
                    if (rth->rta_type == IFA_LOCAL) {
                        uint32_t ipv4_address_host_byte_order = *((uint32_t *)RTA_DATA(rth));
                        uint32_t ipv4_address_network_byte_order = htonl(ipv4_address_host_byte_order);

                        return ipv4_address_network_byte_order;
                    }
                    rth = RTA_NEXT(rth, rtl);
                }
            }
            message = NLMSG_NEXT(message, message_length);
        }
    }

    return 0;
}

int is_captive_portal() {
    CURL *curl_session;
    long response_code;

    if((curl_session = curl_easy_init()) == NULL) {
        return -1;
    }

    curl_easy_setopt(curl_session, CURLOPT_URL, "http://amionacaptiveportal.com/nothing");

    if(curl_easy_perform(curl_session) != CURLE_OK) {
        return -1;
    }

    curl_easy_getinfo(curl_session, CURLINFO_RESPONSE_CODE, &response_code);
    curl_easy_cleanup(curl_session);

    if(response_code == 204) {
        return 0;
    }else{
        return 1;
    }
}

void launch_browser() {
    if(fork() == 0) { // we are the child process
        execve("/usr/bin/xdg-open", (char *[]){ "http://amionacaptiveportal.com/nothing", NULL }, environ);
    }
}

time_t last_liberation = 0;

void liberate() {
    time_t now = time(NULL);
    int remaining_tries = 3;
    int result;

    if(now - last_liberation > 10) {
        last_liberation = now;

        while(remaining_tries-- > 0) {
            result = is_captive_portal();

            if(result == -1) {
                sleep(2);
                continue;
            }

            if(result == 1) {
                launch_browser();
            }

            return;
        }
    }
}

int main(int argc, const char* argv[]) {
    int netlink_socket;

    if((netlink_socket = open_netlink_socket_for_ipv4_address_changes()) < 0) {
        perror("failed to open NETLINK socket for IPv4 address changes");
        return 1;
    }

    do {
        liberate();
    } while(wait_for_new_local_ipv4_address(netlink_socket) != 0);

    return 0;
}