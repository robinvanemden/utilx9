/***************************************************************************
 * Copyright (C) 2017 - 2020, Lanka Hsu, <lankahsu@gmail.com>, et al.
 *
 * This software is licensed as described in the file COPYING, which
 * you should have received as part of this distribution.
 *
 * You may opt to use, copy, modify, merge, publish, distribute and/or sell
 * copies of the Software, and permit persons to whom the Software is
 * furnished to do so, under the terms of the COPYING file.
 *
 * This software is distributed on an "AS IS" basis, WITHOUT WARRANTY OF ANY
 * KIND, either express or implied.
 *
 ***************************************************************************/
#include <netdb.h> // gethostbyname
#include <linux/route.h> // RTF_UP, RTF_GATEWAY
#include <ifaddrs.h> // struct ifaddrs

#include "utilx9.h"

static int chainX_mutex_init(ChainXCtx_t *chainX_req)
{
	int ret = -1;

	int rc = SAFE_MUTEX_ATTR_RECURSIVE(chainX_req->in_mtx);
	if (rc == 0)
	{
		SAFE_COND_ATTR_NORMAL(chainX_req->in_cond);
		ret = 0;
	}

	return ret;
}

#ifdef UTIL_EX_TTY
static int chainX_tty_open(ChainXCtx_t *chainX_req)
{
//	return SAFE_OPEN( chainX_req->ttyinfo.ttyname, O_RDWR | O_NONBLOCK | O_NOCTTY | O_NDELAY );
//	return SAFE_OPEN( chainX_req->ttyinfo.ttyname, O_RDWR | O_NONBLOCK | O_NDELAY );
	return SAFE_OPEN( chainX_req->ttyinfo.ttyname, O_RDWR );
}
#endif

static int chainX_icmp_open(void)
{
	int sockfd = SAFE_SOPEN(AF_INET, SOCK_RAW, IPPROTO_ICMP);

	return sockfd;
}

static int chainX_udp_socket(void)
{
	int sockfd = SAFE_SOPEN(AF_INET, SOCK_DGRAM, IPPROTO_IP);

	if (sockfd>=0)
	{
		int reuse = 1;
		SAFE_SSETOPT(sockfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
	}

	return sockfd;
}

static int chainX_tcp_socket(void)
{
	int sockfd = SAFE_SOPEN(AF_INET, SOCK_STREAM, 0);

	if (sockfd>=0)
	{
		int reuse = 1;
		SAFE_SSETOPT(sockfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

		int nodelay = 1; 
		SAFE_SSETOPT(sockfd, IPPROTO_TCP, TCP_NODELAY, (void *) &nodelay, sizeof(nodelay));
		int keepalive = 1;
		SAFE_SSETOPT(sockfd, SOL_SOCKET, SO_KEEPALIVE, (void *) &keepalive, sizeof(keepalive));
		int keepidle = 20;
		SAFE_SSETOPT(sockfd, SOL_TCP, TCP_KEEPIDLE, (void *) &keepidle, sizeof(keepidle));
		int keepinterval = 5;
		SAFE_SSETOPT(sockfd, SOL_TCP, TCP_KEEPINTVL, (void *) &keepinterval, sizeof(keepinterval));
		int keepcount = 3;
		SAFE_SSETOPT(sockfd, SOL_TCP, TCP_KEEPCNT, (void *) &keepcount, sizeof(keepcount));
	}

	return sockfd;
}

static int chainX_fcntl_socket(ChainXCtx_t * chainX_req)
{
	int ret = 0;
#ifdef UTIL_EX_SOCKET_NOBLOCK
	if (chainX_req->noblock==1)
	{
		int flags = SAFE_FCNTL(chainX_fd_get(chainX_req), F_GETFL, 0);
		/*  Now connect our socket to the server's socket.  */
		flags = (flags|O_NONBLOCK);
		if (SAFE_FCNTL(chainX_fd_get(chainX_req), F_SETFL, flags) < 0 )
		{
			DBG_ER_LN("fcntl O_NONBLOCK error !!! (errno: %d %s)", errno, strerror(errno));
		}
	}
#endif
	return ret;
}

void chainX_addr2ipstr(int family, void *sinx_addr, char *ipstr, int len)
{
	if (ipstr)
	{
		inet_ntop(family, sinx_addr, ipstr, len);
	}
}

int chainX_dns_get(char *dns)
{
	int ret = -1;
	char buf[LEN_OF_NEWLINE] = "", ns_str[LEN_OF_VAL32] = "";

	FILE *fp = SAFE_FOPEN("/etc/resolv.conf", "r");
	if (NULL == fp)
	{
		return ret;
	}
	while (SAFE_FGETS(buf, sizeof(buf), fp) != NULL)
	{
		if (SAFE_STRNCMP(buf, "nameserver", 10) != 0)
		{
			continue;
		}
		if (SAFE_SSCANF(buf, "%s%s", ns_str, dns+strlen(dns) ) == 2 )
		{
			dns[strlen(dns)] = ';';
			ret = 0;
		}
	}
	SAFE_FCLOSE(fp);

	if ( ( strlen(dns) > 0 ) && ( dns[strlen(dns)-1] == ';' ) )
	{
		dns[strlen(dns)-1] = '\0';
	}

	//DBG_ER_LN("(dns: %s)", dns);
	return ret;
}

int chainX_dns_set(char *dns)
{
	int ret = -1;

	if ( dns == NULL )
	{
		DBG_ER_LN("dns is NULL !!!");
		return ret;
	}

	//DBG_ER_LN("(dns: %s)", dns);
	FILE *fp = SAFE_FOPEN("/etc/resolv.conf", "wb");
	if (NULL == fp)
	{
		return ret;
	}
	char *saveptr = NULL;
	char *server = SAFE_STRTOK_R(dns, ";", &saveptr);
	while (server)
	{
		in_addr_t addr = inet_addr(server);
		
		if ( addr != -1 )
		{
			char buf[LEN_OF_BUF256] = "";
			SAFE_SNPRINTF(buf, sizeof(buf), "nameserver %s\n", server);
			SAFE_FPUTS(buf, fp);
			ret = 0;
		}
		else
		{
			ret = -1;
		}
		server = SAFE_STRTOK_R(NULL, ";", &saveptr);
	}
	SAFE_FCLOSE(fp);

	return ret;
}

int chainX_if_staticip(char *iface, char *ip, char *netmask, char *gateway)
{
	int ret = -1;

	if ( iface == NULL )
	{
		DBG_ER_LN("iface is NULL !!!");
		return ret;
	}

	if ( ip == NULL )
	{
		DBG_ER_LN("ip is NULL !!!");
		return ret;
	}

	if ( netmask == NULL )
	{
		DBG_ER_LN("netmask is NULL !!!");
		return ret;
	}

	if ( gateway == NULL )
	{
		DBG_ER_LN("gateway is NULL !!!");
		return ret;
	}

	int sockfd = chainX_udp_socket();
	if (sockfd >= 0)
	{
		struct ifreq ifreq_info;
		memset(&ifreq_info, 0, sizeof(struct ifreq));
		SAFE_SNPRINTF(ifreq_info.ifr_name, IF_NAMESIZE, "%s", iface);

		struct sockaddr_in *ipv4 = (struct sockaddr_in*)&ifreq_info.ifr_addr;
		ipv4->sin_family = AF_INET;

		//ipaddr
		if ( inet_aton(ip, &(ipv4->sin_addr) ) < 0)
		{
			DBG_ER_LN("inet_aton error !!! (ip: %s)", ip);
			goto STATICIP_EXIT;
		}

		if ( ioctl(sockfd, SIOCSIFADDR, &ifreq_info) < 0 )
		{
			DBG_ER_LN("ioctl - SIOCSIFADDR error !!! (ip: %s)", ip);
			goto STATICIP_EXIT;
		}

		//netmask
		if ( inet_aton(netmask, &(ipv4->sin_addr)) < 0 )
		{
			DBG_ER_LN("inet_aton error !!! (netmask: %s)", netmask);
			goto STATICIP_EXIT;
		}

		if ( ioctl(sockfd, SIOCSIFNETMASK, &ifreq_info) < 0 )
		{
			DBG_ER_LN("ioctl - SIOCSIFNETMASK error !!! (netmask: %s)", netmask);
			goto STATICIP_EXIT;
		}

		//gateway
		struct rtentry routex;
		memset(&routex, 0, sizeof(struct rtentry));
		ipv4 = (struct sockaddr_in*) &routex.rt_dst;
		ipv4->sin_family = AF_INET;
		ipv4->sin_addr.s_addr = inet_addr("0.0.0.0");

		ipv4 = (struct sockaddr_in*) &routex.rt_genmask;
		ipv4->sin_family = AF_INET;
		ipv4->sin_addr.s_addr = inet_addr("0.0.0.0");

		routex.rt_dev = iface;
		routex.rt_metric = 1;
		routex.rt_window = 0;
		routex.rt_flags = RTF_UP;

		// errno -l
		// ESRCH 3 No such process
		if ( ( ioctl(sockfd, SIOCDELRT, &routex) < 0 ) && (errno != ESRCH) )
		{
			DBG_ER_LN("ioctl error !!! (errno: %d %s)", errno, strerror(errno));
			goto STATICIP_EXIT;
		}

		ipv4 = (struct sockaddr_in*) &routex.rt_gateway;
		ipv4->sin_family = AF_INET;
		ipv4->sin_port = 0;
		if ( inet_aton(gateway, &ipv4->sin_addr) < 0 )
		{
			DBG_ER_LN("inet_aton error !!! (gateway: %s)", gateway);
			goto STATICIP_EXIT;
		}

		routex.rt_flags = RTF_UP | RTF_GATEWAY;
		if ( ioctl(sockfd, SIOCADDRT, &routex) < 0 )
		{
			DBG_ER_LN("ioctl error !!! (errno: %d %s)", errno, strerror(errno));
			goto STATICIP_EXIT;
		}

		ret = 0;
	}

STATICIP_EXIT:
	SAFE_SCLOSE(sockfd);
	return ret;
}

int chainX_if_list(chainX_if_list_fn list_cb)
{
	int ret = -1;

	struct ifaddrs *ifaddrs_ptr = NULL, *ifa = NULL;

	if ( getifaddrs(&ifaddrs_ptr)== -1)
	{
		DBG_ER_LN("getifaddrs error !!!");
		return ret;
	}

	for (ifa = ifaddrs_ptr; ifa != NULL; ifa = ifa->ifa_next)
	{
		char host[NI_MAXHOST] = "";

		if (ifa->ifa_addr == NULL)
		{
			continue;
		}

		int family = ifa->ifa_addr->sa_family;
		if ( ( family == AF_INET ) || ( family == AF_INET6 ) )
		{ // IPv4 or IPv6
			int s = getnameinfo(ifa->ifa_addr, (family == AF_INET) ? sizeof(struct sockaddr_in) : sizeof(struct sockaddr_in6),	host, NI_MAXHOST, NULL, 0, NI_NUMERICHOST);
			if ( s !=0 )
			{
				DBG_ER_LN("getnameinfo error !!! (%d)", s);
			}
			else
			{
				char netmask[LEN_OF_IP] = "";
				void *addr = &(((struct sockaddr_in*)(ifa->ifa_netmask))->sin_addr);
				SAFE_SPRINTF(netmask, "%s", inet_ntop(AF_INET, addr, netmask, sizeof(netmask)) );

				if (list_cb)
				{
					list_cb(ifa->ifa_name, ifa->ifa_flags, family, host, netmask);
				}
				else
				{
					DBG_ER_LN("(name: %s, flags: 0x%02x, family: %d, host: %s, netmask: %s)", 
										ifa->ifa_name, 
										ifa->ifa_flags, 
										family, 
										host, 
										netmask);
				}
			}
		}
		else if ( family == PF_PACKET )
		{
			char ethmac[LEN_OF_MAC]= "";
			char split[] = ":";
			unsigned char *ptr = (unsigned char *)((struct sockaddr *)(ifa->ifa_addr)->sa_data);
			int i = 0;
			for (i = 0; i < 6; i++)
			{
				//mac[i] = s.ifr_addr.sa_data[i];
				if (i!=0)
					SAFE_SPRINTF(ethmac+strlen(ethmac), "%s%02X", split, (unsigned char)ptr[i]);
				else
					SAFE_SPRINTF(ethmac+strlen(ethmac), "%02X", (unsigned char)ptr[i]);
			}
		}
		else
		{
		}
	}

	freeifaddrs(ifaddrs_ptr);
	return ret;
}

int chainX_if_ipaddr(char *iface, char *ip)
{
	int ret = -1;

	if ( iface == NULL )
	{
		DBG_ER_LN("iface is NULL !!!");
		return ret;
	}

	if ( ip == NULL )
	{
		DBG_ER_LN("ip is NULL !!!");
		return ret;
	}

	int sockfd = chainX_udp_socket();
	if (sockfd >= 0)
	{
		struct ifreq ifreq_info;
		memset(&ifreq_info, 0, sizeof(struct ifreq)); 
		SAFE_SNPRINTF(ifreq_info.ifr_name, IF_NAMESIZE, "%s", iface);
		if (0 == SAFE_IOCTL(sockfd, SIOCGIFADDR, &ifreq_info))
		{
			SAFE_SPRINTF(ip, "%s", inet_ntoa(((struct sockaddr_in *)&ifreq_info.ifr_addr)->sin_addr));
			ret = 0;
		}

		SAFE_SCLOSE(sockfd);
	}
	return ret;
}

int chainX_if_netmask(char *iface, char *netmask)
{
	int ret = -1;

	if ( iface == NULL )
	{
		DBG_ER_LN("iface is NULL !!!");
		return ret;
	}

	if ( netmask == NULL )
	{
		DBG_ER_LN("netmask is NULL !!!");
		return ret;
	}

	int sockfd = chainX_udp_socket();
	if (sockfd >= 0)
	{
		struct ifreq ifreq_info;
		memset(&ifreq_info, 0, sizeof(struct ifreq)); 
		SAFE_SNPRINTF(ifreq_info.ifr_name, IF_NAMESIZE, "%s", iface);
		if (0 == SAFE_IOCTL(sockfd, SIOCGIFNETMASK, &ifreq_info))
		{
			SAFE_SPRINTF(netmask, "%s", inet_ntoa(((struct sockaddr_in *)&ifreq_info.ifr_addr)->sin_addr));
			ret = 0;
		}

		SAFE_SCLOSE(sockfd);
	}
	return ret;
}

int chainX_if_broadcast(char *iface, char *broadcast)
{
	int ret = -1;

	if ( iface == NULL )
	{
		DBG_ER_LN("iface is NULL !!!");
		return ret;
	}

	if ( broadcast == NULL )
	{
		DBG_ER_LN("ip is NULL !!!");
		return ret;
	}

	int sockfd = chainX_udp_socket();
	if (sockfd >= 0)
	{
		struct ifreq ifreq_info;
		memset(&ifreq_info, 0, sizeof(struct ifreq)); 
		SAFE_SNPRINTF(ifreq_info.ifr_name, IF_NAMESIZE, "%s", iface);
		if (0 == SAFE_IOCTL(sockfd, SIOCGIFBRDADDR, &ifreq_info))
		{
			SAFE_SPRINTF(broadcast, "%s", inet_ntoa(((struct sockaddr_in *)&ifreq_info.ifr_broadaddr)->sin_addr));
			ret = 0;
		}

		SAFE_SCLOSE(sockfd);
	}
	return ret;
}

int chainX_if_gateway(char *iface, char *gateway)
{
	int ret = -1;

	if ( iface == NULL )
	{
		DBG_ER_LN("iface is NULL !!!");
		return ret;
	}

	if ( gateway == NULL )
	{
		DBG_ER_LN("gateway is NULL !!!");
		return ret;
	}

	FILE *fp = SAFE_FOPEN("/proc/net/route", "r");
	if (fp)
	{
		int iflags = 0;
		unsigned long dest_addr, gate_addr;
		char newline[LEN_OF_NEWLINE] = "";
		SAFE_FGETS(newline, sizeof(newline), fp); // title
		while (SAFE_FGETS(newline, sizeof(newline), fp))
		{
			char iface_name[LEN_OF_VAL32] = "";
			if ( (SAFE_SSCANF(newline, "%s\t%lX\t%lX\t%X", iface_name, &dest_addr, &gate_addr, &iflags) == 4 )
						&& ( SAFE_STRCMP(iface_name, iface) == 0) ) 
			{
				//if (dest_addr == 0)
				if ( (iflags & (RTF_UP | RTF_GATEWAY)) == (RTF_UP | RTF_GATEWAY) )
				{
					//DBG_ER_LN("(iface_name: %s, dest_addr: %d, gate_addr: %d, iflags: %d)", iface_name, dest_addr, gate_addr, iflags);
					struct in_addr addr;
					memcpy(&addr, &gate_addr, 4);
					SAFE_SPRINTF(gateway, "%s", inet_ntoa(addr));
					ret = 0;
					break;
				}
			}
		}
		SAFE_FCLOSE(fp);
	}

	return ret;
}

int chainX_if_hwaddr(char *iface, char *mac, char *split)
{
	int ret = -1;

	if ( iface == NULL )
	{
		DBG_ER_LN("iface is NULL !!!");
		return ret;
	}

	if ( mac == NULL )
	{
		DBG_ER_LN("mac is NULL !!!");
		return ret;
	}

	mac[0] = '\0';

	int sockfd = chainX_udp_socket();
	if (sockfd >= 0)
	{
		struct ifreq ifreq_info;
		memset(&ifreq_info, 0, sizeof(struct ifreq)); 
		SAFE_SNPRINTF(ifreq_info.ifr_name, IF_NAMESIZE, "%s", iface);
		if (0 == SAFE_IOCTL(sockfd, SIOCGIFHWADDR, &ifreq_info))
		{
			int i = 0;
			for (i = 0; i < 6; i++)
			{
				//mac[i] = s.ifr_addr.sa_data[i];
				if (i!=0)
					SAFE_SPRINTF(mac+strlen(mac), "%s%02X", split,(unsigned char) ifreq_info.ifr_addr.sa_data[i]);
				else
					SAFE_SPRINTF(mac+strlen(mac), "%02X",(unsigned char) ifreq_info.ifr_addr.sa_data[i]);
			}
			ret = 0;
		}
		SAFE_SCLOSE(sockfd);
	}
	return ret;
}

#include <linux/wireless.h>

int chainX_if_ssid(char *iface, char *ssid)
{
	int ret = -1;

	if ( iface == NULL )
	{
		DBG_ER_LN("iface is NULL !!!");
		return ret;
	}

	if ( ssid == NULL )
	{
		DBG_ER_LN("ssid is NULL !!!");
		return ret;
	}

	int sockfd = chainX_udp_socket();
	if (sockfd >= 0)
	{
		char essid[IW_ESSID_MAX_SIZE];
		memset (essid, 0, IW_ESSID_MAX_SIZE);
		 
		struct iwreq iwreq_info;
		memset(&iwreq_info, 0, sizeof(struct iwreq)); 
		SAFE_SNPRINTF(iwreq_info.ifr_name, IF_NAMESIZE, "%s", iface);
		iwreq_info.u.essid.pointer = essid;
		iwreq_info.u.essid.length = IW_ESSID_MAX_SIZE;

		if (0 == SAFE_IOCTL(sockfd, SIOCGIWESSID, &iwreq_info))
		{
			SAFE_SPRINTF(ssid, "%s", essid);
			ret = 0;
		}

		SAFE_SCLOSE(sockfd);
	}
	return ret;
}

int chainX_fd_get(ChainXCtx_t *chainX_req)
{
	if (chainX_req)
	{
#ifdef UTIL_EX_TTY
		if (chainX_req->mode == CHAINX_MODE_ID_TTY)
		{
			return chainX_req->ttyfd;
		}
		else
#endif
		{
			return chainX_req->sockfd;
		}
	}
	else
	{
		return -1;
	}
}

static void chainX_fdset_set(ChainXCtx_t *chainX_req, fd_set *iset)
{
	//SAFE_FD_ZERO(iset);
	//SAFE_FD_SET(chainX_fd_get(chainX_req), iset);

	SAFE_FD_SET_EX(chainX_fd_get(chainX_req), iset);
}

static void chainX_fdset_setall(ChainXCtx_t *chainX_req)
{
	if ( chainX_fd_get(chainX_req) >=0 )
	{
		chainX_fdset_set(chainX_req, &CHAINX_FDSET_R(chainX_req));
		chainX_fdset_set(chainX_req, &CHAINX_FDSET_W(chainX_req));
		chainX_fdset_set(chainX_req, &CHAINX_FDSET_E(chainX_req));
	}
}

static void chainX_fdset_clear(ChainXCtx_t *chainX_req)
{
	if ( chainX_fd_get(chainX_req) >=0 )
	{
		CHAINX_FD_CLR_R(chainX_req);
		CHAINX_FD_CLR_W(chainX_req);
		CHAINX_FD_CLR_E(chainX_req);
	}
}

int chainX_port_get(ChainXCtx_t *chainX_req)
{
	if (chainX_req)
		return chainX_req->netinfo.port;
	else
		return 0;
}

int chainX_port_set(ChainXCtx_t *chainX_req, int port)
{
	int ret = 0;
	if ( (chainX_req) && (port>0) )
	{
		chainX_req->netinfo.port = port;
	}
	else
	{
		DBG_ER_LN("port error (port: %d)", port);
		ret = -1;
	}
	return ret;
}

char * chainX_ip_get(ChainXCtx_t * chainX_req)
{
	if (chainX_req)
	{
		return chainX_req->netinfo.addr.ipv4;
	}
	else 
	{
		return NULL;
	}
}

void chainX_ip_set(ChainXCtx_t *chainX_req, char *ip)
{
	if ( (chainX_req) && (ip) )
	{
		SAFE_SPRINTF(chainX_req->netinfo.addr.ipv4, "%s", ip);
	}
}

struct sockaddr_in * chainX_addr_to_get(ChainXCtx_t * chainX_req)
{
	if (chainX_req)
	{
		return & chainX_req->addr_to;
	}

	else 
	{
		return NULL;
	}
}

int chainX_addr_to_set_ex(ChainXCtx_t *chainX_req, int ai_family, char *ip_addr, int port)
{
	int ret = 0;
	//DBG_IF_LN("(ip_addr: %s:%d)", ip_addr, port);
	switch (ai_family)
	{
		case AF_INET6:
			{
				struct in6_addr in6;
				if (inet_pton(AF_INET6, ip_addr, &in6) != 0)
				{			
					struct sockaddr_in6 *send_addr = (struct sockaddr_in6 *)chainX_addr_to_get(chainX_req);
					/* set up destination address */
					SAFE_MEMSET(send_addr, 0, sizeof(struct sockaddr_in6));
					
					send_addr->sin6_family = AF_INET6;
					send_addr->sin6_addr = in6;
					send_addr->sin6_port = htons(port);
				}
				else
				{
					ret = -1;
				}
			}
			break;
		case AF_INET:
		default:
			{
				struct in_addr in4;
				if (inet_aton(ip_addr, &in4) != 0)
				{
					struct sockaddr_in *send_addr = chainX_addr_to_get(chainX_req);
					/* set up destination address */
					SAFE_MEMSET(send_addr, 0, sizeof(struct sockaddr_in));
					send_addr->sin_family = AF_INET;
					//send_addr->sin_addr.s_addr = inet_addr(host);
					send_addr->sin_addr = in4;
					send_addr->sin_port = htons(port);

					chainX_ip_set(chainX_req, ip_addr);
				}
				else
				{
					ret = -1;
				}
			}
			break;
	}
	return ret;
}

int chainX_addr_to_set(ChainXCtx_t *chainX_req, char *ipv4, int port)
{
	return chainX_addr_to_set_ex(chainX_req, AF_INET, ipv4, port);
}

struct sockaddr_in * chainX_addr_from_get(ChainXCtx_t * chainX_req)
{
	if (chainX_req)
	{
		return & chainX_req->addr_frm;
	}
	else
	{
		return NULL;
	}
}


char *chainX_hostname_get(ChainXCtx_t *chainX_req)
{
	if (chainX_req)
	{
		return chainX_req->netinfo.addr.hostname;
	}
	else
	{
		return NULL;
	}
}

void chainX_hostname_set(ChainXCtx_t *chainX_req, char *hostname)
{
	if ( (chainX_req) && (hostname) )
	{
		SAFE_SPRINTF(chainX_req->netinfo.addr.hostname, "%s", hostname);
	}
}

char *chainX_reversename_get(ChainXCtx_t *chainX_req)
{
	if (chainX_req)
	{
		return chainX_req->netinfo.reversename;
	}
	else
	{
		return 0;
	}
}

void chainX_reversename_set(ChainXCtx_t *chainX_req, char *hostname)
{
	if ( (chainX_req) && (hostname) )
	{
		SAFE_SPRINTF(chainX_req->netinfo.reversename, "%s", hostname);
	}
}

int chainX_security_get(ChainXCtx_t *chainX_req)
{
	if (chainX_req)
	{
		return chainX_req->security;
	}
	else
	{
		return 0;
	}
}

int chainX_security_set(ChainXCtx_t *chainX_req, int mode)
{
	if (chainX_req)
	{
		switch (mode)
		{
			case 1:
				chainX_req->security= mode;
				break;
			default:
				chainX_req->security = 0;
				break;
		}
		return chainX_req->security;
	}
	else
	{
		return 0;
	}
}

int chainX_recycle_get(ChainXCtx_t *chainX_req)
{
	if (chainX_req)
	{
		return chainX_req->recycle;
	}
	else
	{
		return 0;
	}
}

int chainX_recycle_dec(ChainXCtx_t *chainX_req)
{
	if ((chainX_req) && (chainX_req->recycle))
	{
		chainX_req->recycle --;
		return chainX_req->recycle;
	}
	else
	{
		return 0;
	}
}

int chainX_recycle_set(ChainXCtx_t *chainX_req, int recycle)
{
	if (chainX_req)
	{
		chainX_req->recycle= recycle;
		return chainX_req->recycle;
	}
	else
	{
		return 0;
	}
}

int chainX_infinite_get(ChainXCtx_t *chainX_req)
{
	if (chainX_req)
	{
		return chainX_req->infinite;
	
	}
	else
	{
		return 0;
	}
}

int chainX_infinite_set(ChainXCtx_t *chainX_req, int infinite)
{
	if (chainX_req)
	{
		chainX_req->infinite= infinite;
		return chainX_req->infinite;
	}
	else
	{
		return 0;
	}
}

#if (1)
int chainX_nslookup_ex(char *hostname, int ai_family, char *ipv4_addr, char *ipv6_addr)
{
	int ret = -1;
	struct addrinfo hints, *res, *p;
	char ipstr[INET6_ADDRSTRLEN];

	SAFE_MEMSET(&hints, 0, sizeof hints);
	hints.ai_family = ai_family; // AF_UNSPEC, AF_INET or AF_INET6 
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE;

	if ((ret = getaddrinfo(hostname, NULL, &hints, &res)) != 0)
	{
		DBG_ER_LN("getaddrinfo error !!! (%s)", gai_strerror(ret));
		return ret;
	}

	for(p = res;p != NULL; p = p->ai_next)
	{
		void *addr;

		if ((p->ai_family == AF_INET) && (ipv4_addr))
		{ // IPv4
			struct sockaddr_in *ipv4 = (struct sockaddr_in *)p->ai_addr;
			addr = &(ipv4->sin_addr);
			inet_ntop(p->ai_family, addr, ipstr, sizeof(ipstr) );
			SAFE_SPRINTF(ipv4_addr, "%s", ipstr);

			if ( (ai_family==AF_UNSPEC) || (ai_family==AF_INET) )
			{
				// found
				ret = 0;
			}
		}
		else if (ipv6_addr)
		{ // IPv6
			struct sockaddr_in6 *ipv6 = (struct sockaddr_in6 *)p->ai_addr;
			addr = &(ipv6->sin6_addr);
			inet_ntop(p->ai_family, addr, ipstr, sizeof(ipstr) );
			SAFE_SPRINTF(ipv6_addr, "%s", ipstr);

			if ( (ai_family==AF_UNSPEC) || (ai_family==AF_INET6) )
			{
				// found
				ret = 0;
			}
		}
	}
	freeaddrinfo(res);

	return ret;
}

int chainX_nslookup6(char *hostname , char *ip)
{
	return chainX_nslookup_ex(hostname, AF_INET6, NULL, ip);
}

int chainX_nslookup(char *hostname , char *ip)
{
	return chainX_nslookup_ex(hostname, AF_INET, ip, NULL);
}

#else
int chainX_nslookup(char * hostname, char * ip)
{
	struct hostent * he;
	struct in_addr * *addr_list;
	int i;

	if ((he = gethostbyname(hostname)) == NULL)
	{
		// get the host info
		DBG_ER_LN("gethostbyname error !!! (hostname: %s)", hostname);
		return - 1;
	}

	addr_list = (struct in_addr * *)
	he->h_addr_list;

	for (i = 0; addr_list[i] != NULL; i++)
	{
		//Return the first one;
		strcpy(ip, (const char *) inet_ntoa(*addr_list[i]));
		return 0;
	}

	return - 1;
}
#endif

// Resolves the reverse lookup of the hostname 
int chainX_nslookup_reverse(char *ip_addr, char *hostname) 
{
	int ret = -1;

	struct sockaddr_in temp_addr;
	socklen_t len; 
	char buf[NI_MAXHOST]="";

	temp_addr.sin_family = AF_INET;
	temp_addr.sin_addr.s_addr = inet_addr(ip_addr);
	len = sizeof(struct sockaddr_in);

	if (getnameinfo((struct sockaddr *) &temp_addr, len, buf, sizeof(buf), NULL, 0, NI_NAMEREQD))  
	{
		DBG_ER_LN("getnameinfo error !!!");
		return ret;
	}

	if (hostname)
	{
		SAFE_SPRINTF(hostname, "%s", buf);
		ret = 0;
	}
	return ret;
} 

static int chainX_status_check(ChainXCtx_t *chainX_req)
{
	int ret = 0;
	if ( chainX_req )
	{
		if ( 0 == SAFE_THREAD_LOCK_EX(chainX_req) )
		{
			ret = chainX_req->status;
			SAFE_THREAD_UNLOCK_EX(chainX_req);
		}
	}
	return ret;
}

static void chainX_status_set(ChainXCtx_t *chainX_req, int isready)
{
	if (chainX_req)
	{
		if ( 0 == SAFE_THREAD_LOCK_EX(chainX_req) )
		{
			chainX_req->status= isready;
			SAFE_THREAD_UNLOCK_EX(chainX_req);
		}
	}
}

static int chainX_quit_check(ChainXCtx_t *chainX_req)
{
	int ret = -1;
	if ( chainX_req )
	{
		if ( 0 == SAFE_THREAD_LOCK_EX(chainX_req) )
		{
			ret = chainX_req->isquit;
			SAFE_THREAD_UNLOCK_EX(chainX_req);
		}
	}
	return ret;
}

void chainX_quit_set(ChainXCtx_t *chainX_req, int is_quit)
{
	if ( chainX_req )
	{
		if ( 0 == SAFE_THREAD_LOCK_EX(chainX_req) )
		{
			chainX_req->isquit = is_quit;
			SAFE_THREAD_UNLOCK_EX(chainX_req);
		}
	}
}

int chainX_linked_check(ChainXCtx_t *chainX_req)
{
	int ret = -1;
	if (chainX_req)
	{
		if ( 0 == SAFE_THREAD_LOCK_EX(chainX_req) )
		{
			if ( (chainX_status_check(chainX_req)) && (chainX_fd_get(chainX_req)>=0) )
			{
				ret = 0;
			}
			else
			{
				ret = -1;
			}
			SAFE_THREAD_UNLOCK_EX(chainX_req);
		}
	}

	return ret;
}

void chainX_wakeup(ChainXCtx_t *chainX_req)
{
	if (chainX_req)
	{
		if ( 0 == SAFE_THREAD_LOCK_EX(chainX_req) )
		{
			SAFE_THREAD_SIGNAL_EX(chainX_req);
			SAFE_THREAD_UNLOCK_EX(chainX_req);
		}
	}
}

static void chainX_wait(ChainXCtx_t *chainX_req)
{
	if ( (chainX_req) && ( chainX_quit_check(chainX_req)==0) )
	{
		if ( 0 == SAFE_THREAD_LOCK_EX(chainX_req) )
		{
			if (chainX_req->retry_hold>0)
			{
				SAFE_THREAD_TIMEWAIT_EX(chainX_req, chainX_req->retry_hold*1000 );
			}
			else
			{
				SAFE_THREAD_TIMEWAIT_EX(chainX_req, MIN_TIMEOUT_OF_RETRY*1000 );
			}
			SAFE_THREAD_UNLOCK_EX(chainX_req);
		}
	}
}

static int chainX_socket_error(ChainXCtx_t *chainX_req)
{
	int so_error = -1;
	socklen_t err_len = sizeof(so_error);
	if(SAFE_SGETOPT(chainX_fd_get(chainX_req), SOL_SOCKET, SO_ERROR, &so_error, &err_len) < 0)
	{
		DBG_ER_LN("getsockopt error !!! (errno: %d %s)", errno, strerror(errno));
	}
	else if (so_error==0)
	{
		// nothing
	}
	else
	{
		DBG_ER_LN("getsockopt (errno: %d %s, so_error: %d %s)", errno, strerror(errno), so_error, strerror(so_error));
	}
	return so_error;
}

// for connect
// 0: ok, 1: timeout, -1: error
// 0: read, 1: write
static int chainX_socket_select(ChainXCtx_t *chainX_req, int timeout, int rw)
{
	int ret = -1;
	if (chainX_req)
	{
		chainX_fdset_clear(chainX_req);
		chainX_fdset_setall(chainX_req);

		DBG_TR_LN("select ...");
		int result = 0;
		struct timeval tv;
		tv.tv_sec = timeout;             /* 10 second timeout */
		tv.tv_usec = 0;
		if (chainX_security_get(chainX_req)==1)
		{
			if (rw==1)
				result = SAFE_SELECT(chainX_fd_get(chainX_req) + 1, NULL, &CHAINX_FDSET_W(chainX_req), NULL, &tv);
			else
				result = SAFE_SELECT(chainX_fd_get(chainX_req) + 1, &CHAINX_FDSET_R(chainX_req), NULL, NULL, &tv);
		}
		else
		{
			result = SAFE_SELECT(chainX_fd_get(chainX_req) + 1, &CHAINX_FDSET_R(chainX_req), &CHAINX_FDSET_W(chainX_req), NULL, &tv);
		}

		if (result  == -1 )
		{
			DBG_ER_LN("select error !!! (result: %d, errno: %d %s)", result, errno, strerror(errno));
		}
		else if (result  == 0 )
		{
			//DBG_ER_LN("select tomeout !!! (result: %d)", result);
			ret = 1;
		}
		else if (result > 0)
		{
			if ( CHAINX_FD_ISSET_R(chainX_req) || CHAINX_FD_ISSET_W(chainX_req) )
			{
				ret = chainX_socket_error(chainX_req);
			}
			else
			{
				DBG_ER_LN("FD_ISSET error !!! (result: %d, errno: %d %s)", result, errno, strerror(errno));
			}
		}

		chainX_fdset_clear(chainX_req);
	}
	return ret;
}

static int chainX_RW_select(ChainXCtx_t *chainX_req)
{
	int result = 0;

	errno = 0;

	if (chainX_req->select_wait < MIN_TIMEOUT_OF_SELECT)
	{
		struct timeval tv;
		tv.tv_sec = MIN_TIMEOUT_OF_SELECT;
		tv.tv_usec = 0;
		result = SAFE_SELECT(chainX_fd_get(chainX_req)+1, &CHAINX_FDSET_R(chainX_req), (fd_set *)NULL, &CHAINX_FDSET_E(chainX_req), &tv);
	}
	else
	{
		struct timeval tv;
		tv.tv_sec = chainX_req->select_wait;
		tv.tv_usec = 0;
		result = SAFE_SELECT(chainX_fd_get(chainX_req)+1, &CHAINX_FDSET_R(chainX_req), (fd_set *)NULL, &CHAINX_FDSET_E(chainX_req), &tv);
		//result = SAFE_SELECT(chainX_fd_get(chainX_req)+1, &CHAINX_FDSET_R(chainX_req), (fd_set *)NULL, &CHAINX_FDSET_E(chainX_req), NULL);
	}

	return result;
}

#ifdef UTIL_EX_SOCKET_CERT_FILE
void chainXssl_certificate_file(ChainXCtx_t *chainX_req, char *filename)
{
	if (chainX_req)
	{
		SAFE_SPRINTF(chainX_req->certificate_file, "%s", filename);
	}
}

void chainXssl_privatekey_file(ChainXCtx_t *chainX_req, char *filename)
{
	if (chainX_req)
	{
		SAFE_SPRINTF(chainX_req->privatekey_file, "%s", filename);
	}
}

void chainXssl_ca_file(ChainXCtx_t *chainX_req, char *filename)
{
	if (chainX_req)
	{
		SAFE_SPRINTF(chainX_req->ca_file, "%s", filename);
	}
}
#endif

static int chainXssl_check(ChainXCtx_t *chainX_req);

#ifdef UTIL_EX_SOCKET_OPENSSL
#define SOCKET_SSL_DEBUG(x) ERR_print_errors_fp( stderr );

static void chainXssl_init(ChainXCtx_t *chainX_req)
{
	DBG_TR_LN("enter");
	SSL_library_init();
	SSL_load_error_strings();
	OpenSSL_add_all_algorithms();
}

SSL *chainXssl_sslfd_get(ChainXCtx_t *chainX_req)
{
	if (chainX_req)
	{
		return chainX_req->cSSL;
	}
	else
	{
		return NULL;
	}
}

int chainXssl_ctx_cert_readbuf(SSL_CTX *ctxSSL, const unsigned char *buff, size_t len )
{
	int ret = 0;
	BIO *bio = NULL;
	X509 *cert = NULL;

	if ( (bio = BIO_new_mem_buf((char *)buff, -1)) &&
				(cert = PEM_read_bio_X509(bio, NULL, 0, NULL)) &&
				(SSL_CTX_use_certificate( ctxSSL, cert) != 1 ) )
	{
		SOCKET_SSL_DEBUG(stderr);
		DBG_ER_LN("SSL_CTX_use_certificate error !!!");
		ret = -1;
	}

	if (bio)
	{
		BIO_free(bio);
	}
	if (cert)
	{
		X509_free(cert);
	}

	return ret;
}

int chainXssl_ctx_key_readbuf(SSL_CTX *ctxSSL, const unsigned char *buff, size_t len )
{
#if (1)
	int ret = 0;
	BIO *kbio = NULL;
	EVP_PKEY *pkey = NULL;

	if ( (kbio = BIO_new_mem_buf((char *)buff, -1)) &&
			(pkey = PEM_read_bio_PrivateKey(kbio, NULL, 0, NULL)) &&
			(SSL_CTX_use_PrivateKey( ctxSSL, pkey ) != 1 ) )
	{
		SOCKET_SSL_DEBUG(stderr);
		DBG_ER_LN("SSL_CTX_use_PrivateKey error !!!");
		ret = -1;
	}

	if (kbio)
	{
		BIO_free(kbio);
	}
	if (pkey)
	{
		EVP_PKEY_free(pkey);
	}
#else
	// RSA only
	int ret = 0;
	BIO *kbio = NULL;
	RSA *rsa = NULL;

	if ( (kbio = BIO_new_mem_buf((char *)buff, -1)) &&
			(rsa = PEM_read_bio_RSAPrivateKey(kbio, NULL, 0, NULL)) &&
			(SSL_CTX_use_RSAPrivateKey( ctxSSL, rsa ) != 1 ) )
	{
		SOCKET_SSL_DEBUG(stderr);
		DBG_ER_LN("SSL_CTX_use_RSAPrivateKey error !!!");
		ret = -1;
	}

	if (kbio)
	{
		BIO_free(kbio);
	}
	if (rsa)
	{
		RSA_free(rsa);
	}
#endif

	return ret;
}

static int chainXssl_ctx_cert(ChainXCtx_t *chainX_req)
{
	int ret = -1;
#ifdef UTIL_EX_SOCKET_CERT_TXT
	X509 *ca_cert = NULL;
	BIO *ca_bio = NULL;
#endif

	if ( (chainX_req) && (chainX_req->ctxSSL) )
	{
		/* Load the client certificate into the SSL_CTX structure */
#ifdef UTIL_EX_SOCKET_CERT_TXT
		DBG_TR_LN("call SSL_CTX_use_certificate ... ");
		if ( chainXssl_ctx_cert_readbuf(chainX_req->ctxSSL, chainX_req->certificate_txt, chainX_req->certificate_txt_size) != 0 )
#else
		DBG_TR_LN("call SSL_CTX_use_certificate_file ... (%s)", chainX_req->certificate_file);
		if ( SSL_CTX_use_certificate_file( chainX_req->ctxSSL, chainX_req->certificate_file,  SSL_FILETYPE_PEM) <= 0 ) 
#endif
		{
			SOCKET_SSL_DEBUG(stderr);
			DBG_ER_LN("SSL_CTX_use_certificate_XXX error !!!");
			goto CERT_EXIT;
		}

		/* Load the private-key corresponding to the client certificate */
#ifdef UTIL_EX_SOCKET_CERT_TXT
		DBG_TR_LN("call SSL_CTX_use_PrivateKey ...");
		if ( chainXssl_ctx_key_readbuf(chainX_req->ctxSSL, chainX_req->privatekey_txt, chainX_req->privatekey_txt_size) != 0 )
#else
		DBG_TR_LN("call SSL_CTX_use_PrivateKey_file ... (%s)", chainX_req->privatekey_file);
		if ( SSL_CTX_use_PrivateKey_file( chainX_req->ctxSSL, chainX_req->privatekey_file, SSL_FILETYPE_PEM ) <= 0 )
#endif
		{
			SOCKET_SSL_DEBUG(stderr);
			DBG_ER_LN("SSL_CTX_use_PrivateKey_XXX error !!!");
			goto CERT_EXIT;
		}

		DBG_TR_LN("call SSL_CTX_check_private_key ...");
		/* Check if the client certificate and private-key matches */
		if ( !SSL_CTX_check_private_key( chainX_req->ctxSSL))
		{
			DBG_ER_LN("SSL_CTX_check_private_key error !!!");
			goto CERT_EXIT;
		}

		/* Load the RSA CA certificate into the SSL_CTX structure */
		/* This will allow this client to verify the server's     */
		/* certificate.                                           */
		if ( chainX_req->ca_txt_size > 28 )
		{
#ifdef UTIL_EX_SOCKET_CERT_TXT
			X509_STORE *store = SSL_CTX_get_cert_store(chainX_req->ctxSSL);
			if ( (ca_bio = BIO_new_mem_buf((char *)chainX_req->ca_txt, -1)) &&
						(ca_cert = PEM_read_bio_X509(ca_bio, NULL, 0, NULL)) &&
						(X509_STORE_add_cert( store, ca_cert) != 1 ) )
#else
			DBG_TR_LN("call SSL_CTX_load_verify_locations ... (%s)", chainX_req->ca_file);
			if ( !SSL_CTX_load_verify_locations( chainX_req->ctxSSL, chainX_req->ca_file, NULL ) )
#endif
			{
				SOCKET_SSL_DEBUG(stderr);
				DBG_ER_LN("SSL_CTX_load_verify_locations error !!!");
				goto CERT_EXIT;
			}

			/* Set flag in context to require peer (server) certificate verification */
			SSL_CTX_set_verify( chainX_req->ctxSSL, SSL_VERIFY_PEER, NULL);
			SSL_CTX_set_verify_depth( chainX_req->ctxSSL, 1);
		}
		else
		{
			DBG_TR_LN("SSL_CTX_load_verify_locations skip !!!");
		}

		ret = 0;
	}

CERT_EXIT:
#ifdef UTIL_EX_SOCKET_CERT_TXT
	if (ca_bio)
	{
		BIO_free(ca_bio);
	}
	if (ca_cert)
	{
		X509_free(ca_cert);
	}
#endif

	return ret;
}

static int chainXssl_ctx_create(ChainXCtx_t *chainX_req)
{
	DBG_TR_LN("enter");

	int ret = -1;
	if (chainX_req==NULL)
	{
		DBG_ER_LN("chainX_req is NULL !!!");
		return ret;
	}

	const SSL_METHOD* method = SSLv23_client_method();
	//const SSL_METHOD* method = TLSv1_client_method();
	//const SSL_METHOD* method = TLSv1_1_client_method();
	//const SSL_METHOD* method = TLSv1_2_client_method();
	//const SSL_METHOD* method = TLS_client_method();

	chainX_req->ctxSSL = SSL_CTX_new(method);
	if (chainX_req->ctxSSL == NULL)
	{
		DBG_ER_LN("ctxSSL is NULL !!!");
		return ret;
	}

	SSL_CTX_set_options(chainX_req->ctxSSL, SSL_OP_ALL);

	ret = chainXssl_ctx_cert(chainX_req);

	return ret;
}

static int chainXssl_certs_check(ChainXCtx_t *chainX_req)
{
	int ret = 0;

	if (chainX_req==NULL)
	{
		DBG_ER_LN("chainX_req is NULL !!!");
		return -1;
	}

	DBG_IF_LN("(version : %s)", SSL_get_version(chainX_req->cSSL));

	DBG_IF_LN("(SSL_get_cipher : %s)", SSL_get_cipher(chainX_req->cSSL));
	/* Get the server's certificate (optional) */
	X509 *server_cert = SSL_get_peer_certificate(chainX_req->cSSL);
	if ( server_cert != NULL )
	{
		char *line= NULL;

		line = X509_NAME_oneline(X509_get_subject_name(server_cert),0,0);
		if (line)
		{
			DBG_IF_LN("Server certificate - subject (%s)", line);
			SAFE_FREE(line);
		}

		line = X509_NAME_oneline(X509_get_issuer_name(server_cert),0,0);
		if (line)
		{
			DBG_IF_LN("Server certificate - issuer (%s)", line);
			SAFE_FREE(line);
		}

		X509_free(server_cert);

		chainX_status_set(chainX_req, 1);
	}
	else
	{
		DBG_ER_LN("SSL_get_peer_certificate error !!!");
		ret = -1;
	}

	return ret;
}

int chainXssl_link(ChainXCtx_t *chainX_req)
{
	int ret = -1;
	int retry = RETRY_OF_SSL;
	//DBG_TR_LN("enter");

	if ( (chainX_req) && (chainXssl_ctx_create(chainX_req) != 0) )
	{
		DBG_ER_LN("SSL_setup error !!!");
		return ret;
	}

	if (chainX_req->cSSL)
	{
		SSL_free(chainX_req->cSSL);
		chainX_req->cSSL = NULL;
	}

	chainX_req->cSSL = SSL_new(chainX_req->ctxSSL);
	if (chainX_req->cSSL==NULL)
	{
		DBG_ER_LN("cSSL is NULL !!!");
		return ret;
	}
	SSL_set_fd(chainX_req->cSSL, chainX_fd_get(chainX_req));
	SSL_set_connect_state(chainX_req->cSSL);

do_handshake:
	retry --;
	DBG_TR_LN("call SSL_connect ...");
	if (chainX_req->cSSL)
	{
		int handshake = SSL_connect(chainX_req->cSSL);
		switch (handshake)
		{
			case 1:
				DBG_TR_LN("SSL_connect ok !!!");
				ret = 0;
				break;
			default:
				{
					int err = SSL_get_error(chainX_req->cSSL, ret);
					switch (err)
					{
						// 2
						case SSL_ERROR_WANT_READ:
							{
								int sel = chainX_socket_select(chainX_req, TIMEOUT_OF_SSL, 0);
								if ((sel>=0) && (chainX_quit_check(chainX_req)==0) && (retry>=0))
								{
									DBG_TR_LN("retry ... (handshake: %d, err: %d SSL_ERROR_WANT_READ, sel: %d, retry: %d)", handshake, err, sel, retry);
									goto do_handshake;
								}
								else
								{
									DBG_ER_LN("SSL_connect error !!! (handshake: %d, err: %d, retry: %d)", handshake, err, retry);
									ret = -1;
								}
							}
							break;
						// 3
						case SSL_ERROR_WANT_WRITE:
							{
								int sel = chainX_socket_select(chainX_req, TIMEOUT_OF_SSL, 1);
								if ((sel>=0) && (chainX_quit_check(chainX_req)==0) && (retry>=0))
								{
									DBG_TR_LN("retry ... (handshake: %d, err: %d SSL_ERROR_WANT_WRITE, sel: %d, retry: %d)", handshake, err, sel, retry);
									goto do_handshake;
								}
								else
								{
									DBG_ER_LN("SSL_connect error !!! (handshake: %d, err: %d, retry: %d)", handshake, err, retry);
									ret = -1;
								}
							}
							break;
						// 1
						case SSL_ERROR_SSL:
						// 5
						case SSL_ERROR_SYSCALL:
						default:
							DBG_ER_LN("SSL_connect error !!! (handshake: %d, err: %d, retry: %d)", handshake, err, retry);
							ret = -1;
							break;
					}
				}
				break;
		}
	}

	if (ret==0)
	{
		ret = chainXssl_check(chainX_req);
	}
	return ret;
}

#elif defined(UTIL_EX_SOCKET_MBEDTLS)
static void chainXssl_debug( void *ctx, int level, const char *file, int line, const char *str )
{
	// 1-> 3 PRINT_LEVEL_WARNING
	// 2-> 2 PRINT_LEVEL_INFO
	// 3-> 1 PRINT_LEVEL_DEBUG
	// 4-> 0 PRINT_LEVEL_TRACE
	int lvl = 4-level;
	DBG_PRINT(lvl, "ssl", file, line);
	printf("%s"COLOR_NONE, str);
}

static int chainXssl_myrand( void *rng_state, unsigned char *output, size_t len )
{
	size_t use_len;
	int rnd;

	if( rng_state != NULL )
	rng_state  = NULL;

	while( len > 0 )
	{
		use_len = len;
		if( use_len > sizeof(int) )
		{
			use_len = sizeof(int);
		}

		rnd = rand();
		memcpy( output, &rnd, use_len );
		output += use_len;
		len -= use_len;
	}

	return( 0 );
}

static void chainXssl_init(ChainXCtx_t *chainX_req)
{
	DBG_TR_LN("enter");

	mbedtls_net_init( &chainX_req->server_fd );
	mbedtls_ssl_init( &chainX_req->ssl );
	mbedtls_ssl_config_init( &chainX_req->conf );
	mbedtls_x509_crt_init( &chainX_req->cacert);
	mbedtls_x509_crt_init( &chainX_req->clicert);
	mbedtls_pk_init( &chainX_req->pkey );
	mbedtls_ctr_drbg_init( &chainX_req->ctr_drbg );
	mbedtls_entropy_init( &chainX_req->entropy );
	chainX_req->seed = NULL;

#if defined(MBEDTLS_DEBUG_C)
	mbedtls_debug_set_threshold( SOCKET_SSL_LEVEL );
#endif
}

mbedtls_ssl_context *chainXssl_sslfd_get(ChainXCtx_t *chainX_req)
{
	if (chainX_req)
	{
		return & chainX_req->ssl;
	}
	else
	{
		return NULL;
	}
}

static int chainXssl_ctx_cert(ChainXCtx_t *chainX_req)
{
	int ret = -1;
	if( mbedtls_x509_crt_parse( &chainX_req->clicert, chainX_req->certificate_txt, chainX_req->certificate_txt_size) != 0 )
	{
		DBG_ER_LN("mbedtls_x509_crt_parse error !!! (clicert)");
		return ret;
	}

	if( mbedtls_pk_parse_key( &chainX_req->pkey, chainX_req->privatekey_txt, chainX_req->privatekey_txt_size, NULL, 0) != 0 )
	{
		DBG_ER_LN("mbedtls_pk_parse_key error !!!");
		return ret;
	}

	if( mbedtls_x509_crt_parse( &chainX_req->cacert, chainX_req->ca_txt, chainX_req->ca_txt_size) < 0 )
	{
		DBG_ER_LN("mbedtls_x509_crt_parse error !!! (cacert)");
		return ret;
	}

	ret = 0;
	return ret;
}

static int chainXssl_ctx_create(ChainXCtx_t *chainX_req)
{
	DBG_TR_LN("enter");

	int ret = -1;
	if (chainX_req==NULL)
	{
		DBG_ER_LN("chainX_req is NULL !!!");
		return ret;
	}

	if ( (chainX_req->seed) &&
			( ( mbedtls_ctr_drbg_seed( &chainX_req->ctr_drbg, mbedtls_entropy_func, &chainX_req->entropy, (const unsigned char *) chainX_req->seed, strlen( chainX_req->seed ) ) ) != 0 )
			)
	{
		DBG_ER_LN("mbedtls_ctr_drbg_seed error !!!");
		return ret;
	}
	else if( mbedtls_ctr_drbg_seed( &chainX_req->ctr_drbg, chainXssl_myrand, NULL, NULL, 0 ) != 0 )
	{
		DBG_ER_LN("mbedtls_ctr_drbg_seed error !!!");
		return ret;
	}

	if ( chainXssl_ctx_cert(chainX_req) != 0 )
	{
		return ret;
	}

	if( mbedtls_ssl_config_defaults( &chainX_req->conf, MBEDTLS_SSL_IS_CLIENT, MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT ) != 0 )
	{
		DBG_ER_LN("mbedtls_ssl_config_defaults error !!!");
		return ret;
	}

	mbedtls_ssl_conf_dbg( &chainX_req->conf, chainXssl_debug, stdout );

	mbedtls_ssl_conf_authmode( &chainX_req->conf, MBEDTLS_SSL_VERIFY_NONE );
	mbedtls_ssl_conf_ca_chain( &chainX_req->conf, &chainX_req->cacert, NULL );
	mbedtls_ssl_conf_rng( &chainX_req->conf, mbedtls_ctr_drbg_random, &chainX_req->ctr_drbg );

	if ( mbedtls_ssl_conf_own_cert( &chainX_req->conf, &chainX_req->clicert, &chainX_req->pkey ) != 0 )
	{
		DBG_ER_LN("mbedtls_ssl_conf_own_cert error !!!");
		return ret;
	}

	if( mbedtls_ssl_setup( &chainX_req->ssl, &chainX_req->conf ) != 0 )
	{
		DBG_ER_LN("mbedtls_ssl_setup error !!!");
		return ret;
	}

	if( mbedtls_ssl_set_hostname( &chainX_req->ssl, chainX_req->netinfo.addr.hostname ) != 0 )
	{
		DBG_ER_LN( " mbedtls_ssl_set_hostname error !!!");
		return ret;
	}

	ret = 0;

	return ret;
}

static int chainXssl_certs_check(ChainXCtx_t *chainX_req)
{
	int ret = 0;

	if (chainX_req==NULL)
	{
		DBG_ER_LN("chainX_req is NULL !!!");
		return -1;
	}

	DBG_IF_LN("(SSL_get_cipher : %s)", mbedtls_ssl_get_ciphersuite(&chainX_req->ssl));
	unsigned long flags;
	if( ( flags = mbedtls_ssl_get_verify_result( &chainX_req->ssl ) ) != 0 )
	{
		char vrfy_buf[512];

		DBG_IF_LN("mbedtls_ssl_get_verify_result error !!!" );

		mbedtls_x509_crt_verify_info( vrfy_buf, sizeof( vrfy_buf ), "  ! ", flags );

		DBG_IF_LN("(vrfy_buf: %s)", vrfy_buf );
	}
	else
	{
		DBG_IF_LN("mbedtls_ssl_get_verify_result ok !!!" );
	}

	unsigned char buf[LEN_OF_NEWLINE] = "";
	mbedtls_x509_crt_info( (char *) buf, sizeof( buf ) - 1, "	=>", mbedtls_ssl_get_peer_cert( &chainX_req->ssl ) );
	DBG_IF_LN("(crt_info: %s)", buf);

	chainX_status_set(chainX_req, 1);

	return ret;
}

int chainXssl_link(ChainXCtx_t *chainX_req)
{
	int ret = -1;
	int retry = RETRY_OF_SSL;
	//DBG_TR_LN("enter");

	if ( (chainX_req) && (chainXssl_ctx_create(chainX_req) != 0) )
	{
		DBG_ER_LN("SSL_setup error !!!");
		return ret;
	}

	chainX_req->server_fd.fd = chainX_fd_get(chainX_req);

	mbedtls_ssl_set_bio( &chainX_req->ssl, &chainX_req->server_fd, mbedtls_net_send, mbedtls_net_recv, NULL );

do_handshake:
	retry --;
	DBG_TR_LN("call SSL_connect ...");
	FREE_HEAP_INFO;
	int handshake = mbedtls_ssl_handshake( &chainX_req->ssl );
	switch (handshake)
	{
		case 0:
			DBG_TR_LN("SSL_connect ok !!!");
			ret = 0;
			break;
		// -0x6900
		case MBEDTLS_ERR_SSL_WANT_READ:
			{
				int sel = chainX_socket_select(chainX_req, TIMEOUT_OF_SSL, 0);
				if ((sel>=0) && (chainX_quit_check(chainX_req)==0) && (retry>=0))
				{
					DBG_TR_LN("retry ... (handshake: %d MBEDTLS_ERR_SSL_WANT_READ, sel: %d, retry: %d)", handshake, sel, retry);
					goto do_handshake;
				}
				else
				{
					DBG_ER_LN("SSL_connect error !!! (handshake: %d MBEDTLS_ERR_SSL_WANT_READ, retry: %d)", handshake, retry);
					ret = -1;
				}
			}
			break;
		// -0x6880
		case MBEDTLS_ERR_SSL_WANT_WRITE:
			{
				int sel = chainX_socket_select(chainX_req, TIMEOUT_OF_SSL, 1);
				if ((sel>=0) && (chainX_quit_check(chainX_req)==0) && (retry>=0))
				{
					DBG_TR_LN("retry ... (handshake: %d MBEDTLS_ERR_SSL_WANT_WRITE, sel: %d, retry: %d)", handshake, sel, retry);
					goto do_handshake;
				}
				else
				{
					DBG_ER_LN("SSL_connect error !!! (handshake: %d MBEDTLS_ERR_SSL_WANT_WRITE, retry: %d)", handshake, retry);
					ret = -1;
				}
			}
			break;
		// -0x4300-0x0010
		case (MBEDTLS_ERR_RSA_PRIVATE_FAILED + MBEDTLS_ERR_MPI_ALLOC_FAILED):
		default:
			DBG_ER_LN("SSL_connect error !!! (handshake: 0x%04x, retry: %d)", (-handshake), retry);
			ret = -1;
			break;
	}
	FREE_HEAP_INFO;

	if (ret==0)
	{
		ret = chainXssl_check(chainX_req);
	}
		return ret;
}
#else
static void chainXssl_init(ChainXCtx_t *chainX_req)
{
}

int chainXssl_link(ChainXCtx_t *chainX_req)
{
	int ret = 0;
	return ret;
}
#endif

static int chainXssl_check(ChainXCtx_t *chainX_req)
{
	int ret = -1;
#ifdef UTIL_EX_SSL
	if (chainX_req)
	{
		ret = chainXssl_certs_check(chainX_req);
	}
#endif
	return ret;
}

static void chainXssl_close(ChainXCtx_t *chainX_req)
{
	if ( (chainX_req) && (chainX_security_get(chainX_req) == 1))
	{
		//DBG_TR_LN("enter");
#ifdef UTIL_EX_SOCKET_OPENSSL
		if (chainX_req->cSSL)
		{
			DBG_TR_LN("call SSL_shutdown ...");
			SSL_shutdown(chainX_req->cSSL);
			SSL_free(chainX_req->cSSL);
			chainX_req->cSSL = NULL;
		}

		if (chainX_req->ctxSSL)
		{
			SSL_CTX_free(chainX_req->ctxSSL);
			chainX_req->ctxSSL  = NULL;
		}
#elif defined (UTIL_EX_SOCKET_MBEDTLS)
		if (chainX_req->server_fd.fd >=0 )
		{
			mbedtls_net_free( &chainX_req->server_fd );
			mbedtls_x509_crt_free( &chainX_req->cacert );
			mbedtls_x509_crt_free( &chainX_req->clicert );
			mbedtls_pk_free( &chainX_req->pkey );
			mbedtls_ssl_free( &chainX_req->ssl );
			mbedtls_ssl_config_free( &chainX_req->conf );
			mbedtls_ctr_drbg_free( &chainX_req->ctr_drbg );
			mbedtls_entropy_free( &chainX_req->entropy );
			chainX_req->server_fd.fd = -1;
		}
#endif
		//DBG_TR_LN("exit");
	}
}

void chainX_close(ChainXCtx_t *chainX_req)
{
	if (chainX_req)
	{
		//DBG_TR_LN("enter");
		if ( 0 == SAFE_THREAD_LOCK_EX(chainX_req) )
		{
			chainXssl_close(chainX_req);

			if (chainX_fd_get(chainX_req) >=0)
			{
				chainX_status_set(chainX_req, 0);

#ifdef UTIL_EX_TTY
				if (chainX_req->mode == CHAINX_MODE_ID_TTY)
				{
					SAFE_SCLOSE(chainX_req->ttyfd);
				}
				else
#endif
				{
					SAFE_SCLOSE(chainX_req->sockfd);
				}
			}
			SAFE_THREAD_UNLOCK_EX(chainX_req);
		}
		//DBG_TR_LN("exit");
	}
}

static int chainX_udp_bind(ChainXCtx_t *chainX_req, int multi)
{
	int ret = -1;

	if (chainX_fd_get(chainX_req)>=0)
	{
		chainX_fcntl_socket(chainX_req);

		struct sockaddr_in local_addr;

		SAFE_MEMSET(&local_addr, 0, sizeof(local_addr));
		local_addr.sin_family=AF_INET;
		local_addr.sin_addr.s_addr=htonl(INADDR_ANY); /* N.B.: differs from sender */
		local_addr.sin_port=htons(chainX_req->netinfo.port);

		DBG_IF_LN("bind ... (%s:%u)", chainX_req->netinfo.addr.ipv4, chainX_req->netinfo.port);

		/* bind to receive address */
		if (SAFE_BIND(chainX_fd_get(chainX_req), (struct sockaddr *)&local_addr, sizeof(local_addr)) < 0)
		{
			DBG_ER_LN("bind error !!!");
			ret = -1;
		}

		if (multi)
		{
			struct ip_mreq seMember;

			/* use setsockopt() to request that the kernel join a multicast group */
			SAFE_MEMSET((unsigned char *)&seMember, 0x00, sizeof(seMember));
			seMember.imr_multiaddr.s_addr = inet_addr(chainX_req->netinfo.addr.ipv4);
			seMember.imr_interface.s_addr = htonl(INADDR_ANY);
			if (SAFE_SSETOPT(chainX_fd_get(chainX_req), IPPROTO_IP, IP_ADD_MEMBERSHIP, &seMember, sizeof(seMember)) < 0) 
			{
				DBG_ER_LN("SAFE_SSETOPT IP_ADD_MEMBERSHIP error !!!");
				ret = -1;
			}
			else
			{
				chainX_status_set(chainX_req, 1);
				ret = 0;
			}
		}
		else
		{
			chainX_status_set(chainX_req, 1);
			ret = 0;
		}

		if (ret != 0)
		{
			chainX_close(chainX_req);
		}
	}

	return ret;
}

static int chainX_tcp_connect(ChainXCtx_t *chainX_req)
{
	int ret = -1;

	if (chainX_fd_get(chainX_req)>=0)
	{
		chainX_fcntl_socket(chainX_req);

#ifdef UTIL_EX_SOCKET_BIND_DEV
		{
			char bind_dev[] = "ra0";//"eth2.2";
			if ( SAFE_SSETOPT(chainX_fd_get(chainX_req), SOL_SOCKET, SO_BINDTODEVICE, bind_dev, strlen(bind_dev)+1) )
			{
				DBG_ER_LN("SAFE_SSETOPT SO_BINDTODEVICE error !!! (errno: %d %s)", errno, strerror(errno));
			}
			else
			{
				DBG_TR_LN("SAFE_SSETOPT SO_BINDTODEVICE (bind_dev: %s)", bind_dev);
			}
		}
#endif
		DBG_IF_LN("connecting ... (%s:%u)", chainX_req->netinfo.addr.ipv4, chainX_req->netinfo.port);

		struct sockaddr_in address;
		address.sin_family = AF_INET;
		address.sin_addr.s_addr = inet_addr(chainX_req->netinfo.addr.ipv4);
		address.sin_port = htons(chainX_req->netinfo.port);

		errno = 0;
		int result = SAFE_CONNECT(chainX_fd_get(chainX_req), (struct sockaddr *)&address, sizeof(struct sockaddr));
		//DBG_ER_LN("after connect (result: %d, errno: %d %s)", result, errno, strerror(errno));
		if (result==0)
		{
			if (chainX_security_get(chainX_req) == 1)
			{
			}
			else
			{
				chainX_status_set(chainX_req, 1);
			}
			ret = 0;
		}
		else if (errno == EINPROGRESS)
		{
			if (chainX_security_get(chainX_req) == 1)
			{
				// wait for SSL_connect
				ret = 0;
			}
			else
			{
				int sel = chainX_socket_select(chainX_req, TIMEOUT_OF_SOCKET, 0);
				if (sel==0)
				{
					chainX_status_set(chainX_req, 1);
					ret = 0;
				}
			}
		}
		else
		{
			DBG_ER_LN("connect error !!! (result: %d, errno: %d %s)", result, errno, strerror(errno));
		}

		if (ret != 0)
		{
			chainX_close(chainX_req);
		}
	}

	return ret;
}

#ifdef UTIL_EX_TTY
int speed_arr[] = {
	B230400,
	B115200,
	B57600,
	B38400,
	B19200,
	B9600,
	B4800,
	B2400,
	B1200,
	B300};
int name_arr[] = {
	230400,
	115200,
	57600,
	38400,
	19200,
	9600,
	4800,
	2400,
	1200,
	300};

char *chainX_tty_getname(ChainXCtx_t *chainX_req)
{
	return chainX_req->ttyinfo.ttyname;
}

void chainX_tty_setname(ChainXCtx_t *chainX_req, char *ttyname)
{
	if ( (chainX_req) && (ttyname) )
	{
		SAFE_SPRINTF(chainX_req->ttyinfo.ttyname, "%s", ttyname);
	}
}

void chainX_tty_setbaudrate(ChainXCtx_t *chainX_req, int baudrate)
{
	int idx = 0;

	if ( (chainX_req) )
	{
		for ( idx= 0; idx < sizeof(name_arr) / sizeof(int);	idx++)
		{
			if (baudrate == name_arr[idx])
			{
				chainX_req->ttyinfo.speed = baudrate;
				return;
			}
		}
	}
}

void chainX_tty_setparity(ChainXCtx_t *chainX_req, char parity)
{
	if ( (chainX_req) )
	{
		chainX_req->ttyinfo.parity = parity;
	}
}

void chainX_tty_setdatabits(ChainXCtx_t *chainX_req, char databits)
{
	if ( (chainX_req) )
	{
		chainX_req->ttyinfo.databits = databits;
	}
}

static int chainX_tty_check(ChainXCtx_t *chainX_req)
{
	int ret = 0;
	if ( strlen(chainX_tty_getname(chainX_req)) > 0)
	{
	}
	else
	{
		DBG_ER_LN("chainX_tty_getname error !!!");
		ret = -1;
	}
	return ret;
}

static int chainX_tty_parity(ChainXCtx_t *chainX_req)
{
	int ret = -1;

	chainX_req->ttyinfo.options.c_cflag &= ~CSIZE;
	switch (chainX_req->ttyinfo.databits) /*設置數據位元數 */
	{
		case 7:
			chainX_req->ttyinfo.options.c_cflag |= CS7;
			break;
		case 8:
			chainX_req->ttyinfo.options.c_cflag |= CS8;
			break;
		default:
			DBG_ER_LN("Unsupported data size !!! (databits: %d)", chainX_req->ttyinfo.databits);
			return ret;
	}

	switch (chainX_req->ttyinfo.parity)
	{
		case 'n':
		case 'N':
			chainX_req->ttyinfo.options.c_cflag &= ~PARENB; /* Clear parity enable */
			chainX_req->ttyinfo.options.c_iflag &= ~INPCK; /* Enable parity checking */
			break;

		case 'o':
		case 'O':
			chainX_req->ttyinfo.options.c_cflag |= (PARODD | PARENB); /* 設置為奇效驗 */
			chainX_req->ttyinfo.options.c_iflag |= INPCK; /* Disnable parity checking */
			break;

		case 'e':
		case 'E':
			chainX_req->ttyinfo.options.c_cflag |= PARENB; /* Enable parity */
			chainX_req->ttyinfo.options.c_cflag &= ~PARODD; /* 設置為偶效驗 */
			chainX_req->ttyinfo.options.c_iflag |= INPCK; /* Disnable parity checking */
			break;

		case 'S':
		case 's':  /*as no parity*/
			chainX_req->ttyinfo.options.c_cflag &= ~PARENB;
			chainX_req->ttyinfo.options.c_cflag &= ~CSTOPB;
			break;

		default:
			DBG_ER_LN("Unsupported parity !!! (parity: %c)", chainX_req->ttyinfo.parity);
			return ret;
	}

	/* 設置停止位 */
	switch (chainX_req->ttyinfo.stopbits)
	{
		case 1:
			chainX_req->ttyinfo.options.c_cflag &= ~CSTOPB;
			break;

		case 2:
			chainX_req->ttyinfo.options.c_cflag |= CSTOPB;
			break;

		default:
			DBG_ER_LN("Unsupported stop bits !!! (stopbits: %d)", chainX_req->ttyinfo.stopbits);
			return ret;
	}

	/* Set input parity option */
	if (chainX_req->ttyinfo.parity != 'n')
	{
		chainX_req->ttyinfo.options.c_iflag |= INPCK;
	}

	chainX_req->ttyinfo.options.c_cc[VTIME] = 150; // 15 seconds
	chainX_req->ttyinfo.options.c_cc[VMIN] = 0;

	tcflush(chainX_req->ttyfd, TCIFLUSH); /* Update the options and do it NOW */

	if (tcsetattr(chainX_req->ttyfd, TCSANOW, &chainX_req->ttyinfo.options) != 0)
	{
		DBG_ER_LN("tcsetattr error !!!");
		return ret;
	}

	return 0;
}

static int chainX_tty_speed(ChainXCtx_t *chainX_req)
{
	int idx = 0;

	for ( idx= 0;	idx < sizeof(speed_arr) / sizeof(int);	idx++)
	{
		if	(chainX_req->ttyinfo.speed == name_arr[idx])
		{
			tcflush(chainX_req->ttyfd, TCIOFLUSH);
			cfsetispeed(&chainX_req->ttyinfo.options, speed_arr[idx]);
			cfsetospeed(&chainX_req->ttyinfo.options, speed_arr[idx]);
			if	( tcsetattr(chainX_req->ttyfd, TCSANOW, &chainX_req->ttyinfo.options) != 0)
			{
				DBG_ER_LN("tcsetattr error !!!");
				return -1;
			}
			tcflush(chainX_req->ttyfd, TCIOFLUSH);
		}
	}
	return 0;
}

static int chainX_tty_connect(ChainXCtx_t *chainX_req)
{
	int ret = -1;
	
	if (chainX_fd_get(chainX_req)>=0)
	{
		DBG_IF_LN("connecting ... (%s)", chainX_req->ttyinfo.ttyname);

		if ( tcgetattr(chainX_req->ttyfd, &chainX_req->ttyinfo.options) !=	0)
		{
			DBG_ER_LN("tcgetattr error !!!");
			return ret;
		}
#if (0)
		chainX_req->ttyinfo.options.c_cflag |= (CLOCAL | CREAD | CS8);
		chainX_req->ttyinfo.options.c_cflag &= ~(CSTOPB | PARENB | CRTSCTS);
		/* 設定速度 */
		chainX_req->ttyinfo.options.c_cflag &= ~(CBAUD);
		chainX_req->ttyinfo.options.c_cflag |= B57600;
		chainX_req->ttyinfo.options.c_lflag =0;
		chainX_req->ttyinfo.options.c_oflag =0;
		chainX_req->ttyinfo.options.c_iflag =0;
		chainX_req->ttyinfo.options.c_cc[VINTR]  = 0; //Ctrl-c
		chainX_req->ttyinfo.options.c_cc[VQUIT]  = 0; //Ctrl-
		chainX_req->ttyinfo.options.c_cc[VERASE] = 0; //del
		chainX_req->ttyinfo.options.c_cc[VKILL]  = 0;
		chainX_req->ttyinfo.options.c_cc[VEOF]	 = 0; //Ctrl-d
		chainX_req->ttyinfo.options.c_cc[VTIME]  = 1;
		chainX_req->ttyinfo.options.c_cc[VMIN]	 = 1; // 設定滿足讀取功能的最低字元接收個數
		chainX_req->ttyinfo.options.c_cc[VSWTC]  = 0;
		chainX_req->ttyinfo.options.c_cc[VSTART] = 0; //Ctrl-q
		chainX_req->ttyinfo.options.c_cc[VSTOP]  = 0; //Ctrl-s
		chainX_req->ttyinfo.options.c_cc[VSUSP]  = 0; //Ctrl-z
		chainX_req->ttyinfo.options.c_cc[VEOL]	 = 0;
		chainX_req->ttyinfo.options.c_cc[VREPRINT]=0; //Ctrl-r
		chainX_req->ttyinfo.options.c_cc[VDISCARD]=0; //Ctrl-u
		chainX_req->ttyinfo.options.c_cc[VWERASE]= 0; //Ctrl-w
		chainX_req->ttyinfo.options.c_cc[VLNEXT] = 0; //Ctrl-v
		chainX_req->ttyinfo.options.c_cc[VEOL2]  = 0;
		tcsetattr (chainX_req->ttyfd, TCSANOW, &chainX_req->ttyinfo.options);
		ret = 0;
#else
		if ( chainX_tty_speed(chainX_req) ==	0)
		{
			if ( chainX_tty_parity(chainX_req) == 0)
			{
				ret = 0;
			}
		}
#endif

		if (ret == 0)
		{
			chainX_status_set(chainX_req, 1);
		}
	}

	return ret;
}
#endif

static int chainX_netinfo_scan(ChainXCtx_t *chainX_req)
{
	int ret = -1;
	if ( (chainX_req->netinfo.addr.ipv4) && (strlen(chainX_req->netinfo.addr.ipv4) > 0 ) )
	{
		// Got IPv4 !!!
		ret = 0;
	}
	else if ( ( chainX_req->netinfo.addr.hostname ) && (strlen(chainX_req->netinfo.addr.hostname) > 0 ) )
	{
		// Got hostname -> IPv4 !!!
		if ( chainX_nslookup(chainX_req->netinfo.addr.hostname , chainX_req->netinfo.addr.ipv4) != 0 )
		{
			DBG_ER_LN("chainX_nslookup error !!! (hostname: %s)", chainX_req->netinfo.addr.hostname);
			return -1;
		}
		ret = 0;
	}
	return ret;
}

static int chainX_netinfo_check(ChainXCtx_t *chainX_req)
{
	int ret = -1;
	if ( ( chainX_req->netinfo.addr.hostname ) && (strlen(chainX_req->netinfo.addr.hostname) > 0 ) )
	{
		// Got hostname !!!
		ret = 0;
	}
	else if ( (chainX_req->netinfo.addr.ipv4) && (strlen(chainX_req->netinfo.addr.ipv4) > 0 ) )
	{
		// Got IPv4 !!!
		ret = 0;
	}
	return ret;
}

static int chainX_check(ChainXCtx_t *chainX_req)
{
	int ret = -1;

	if (chainX_req==NULL) return ret;

	/*  Create a socket for the client.  */
	switch (chainX_req->mode)
	{
		case CHAINX_MODE_ID_TCP_CLIENT:
			ret = chainX_netinfo_check(chainX_req);
			break;
		case CHAINX_MODE_ID_UDP_SERVER:
			ret = chainX_netinfo_check(chainX_req);
			break;
		case CHAINX_MODE_ID_MULTI_RECEIVER:
			ret = chainX_netinfo_check(chainX_req);
			break;
#ifdef UTIL_EX_TTY
		case CHAINX_MODE_ID_TTY:
			ret = chainX_tty_check(chainX_req);
			break;
#endif
		default:
			break;
	}
	return ret;
}

static int chainX_init(ChainXCtx_t *chainX_req)
{
	int ret = -1;

	if (chainX_req==NULL) return ret;

	/*  Create a socket for the client.  */
	switch (chainX_req->mode)
	{
		case CHAINX_MODE_ID_TCP_CLIENT:
			{
				if ( 0 == chainX_netinfo_scan(chainX_req) )
				{
					chainX_req->sockfd = chainX_tcp_socket();
					ret = chainX_tcp_connect(chainX_req);
				}
			}
			break;
		case CHAINX_MODE_ID_UDP_SERVER:
			{
				if ( 0 == chainX_netinfo_scan(chainX_req) )
				{
					chainX_req->sockfd = chainX_udp_socket();
					ret = chainX_udp_bind(chainX_req, 0);
				}
			}
			break;
		case CHAINX_MODE_ID_MULTI_RECEIVER:
			{
				if ( 0 == chainX_netinfo_scan(chainX_req) )
				{
					chainX_req->sockfd = chainX_udp_socket();
					ret = chainX_udp_bind(chainX_req, 1);
				}
			}
			break;
#ifdef UTIL_EX_TTY
		case CHAINX_MODE_ID_TTY:
			{
				chainX_req->ttyfd = chainX_tty_open(chainX_req);
				ret = chainX_tty_connect(chainX_req);
			}
			break;
#endif
		default:
			break;
	}
	return ret;
}

void chainX_linked_register(ChainXCtx_t *chainX_req, chainX_linked_fn cb)
{
	if (chainX_req)
	{
		chainX_req->linked_cb = cb;
	}
}

// for tty
#ifdef UTIL_EX_TTY
void chainX_serial_register(ChainXCtx_t *chainX_req, chainX_serial_fn cb)
{
	chainX_req->serial_cb = cb;
}

static void chainX_loop_serial(ChainXCtx_t *chainX_req)
{
	if (chainX_req==NULL) return;

	if (chainX_req->linked_cb) chainX_req->linked_cb(chainX_req);

	while ( ( chainX_quit_check(chainX_req)== 0 ) && ( chainX_linked_check(chainX_req) == 0 ) &&
					((chainX_infinite_get(chainX_req)) || (chainX_recycle_get(chainX_req) > 0)) )
	{
		int result = 0;
		int nread = 0;

		chainX_fdset_clear(chainX_req);
		chainX_fdset_setall(chainX_req);

		if ( chainX_infinite_get(chainX_req) == 0 )
		{
			chainX_recycle_dec(chainX_req);
		}

		result = chainX_RW_select(chainX_req);
		if(result == -1)
		{
			if (errno==EINTR) continue;
			DBG_TR_LN("select error !!! (result: %d, errno: %d %s)", result, errno, strerror(errno));
			break;
		}
		else if (result==0)
		{
		}
		else if ( CHAINX_FD_ISSET_R(chainX_req) ) 
		{
			{
				//nread = LEN_OF_SSL_BUFFER;
				SAFE_IOCTL(chainX_fd_get(chainX_req), FIONREAD, &nread);
			}

			if (nread>0)
			{
				size_t read_pos = 0;
				int read_len = 0;
				size_t left_len = nread;
				char *buff = SAFE_CALLOC(1, nread+1);
				if (buff)
				{
					char *buff_cur = buff;

					while ( (buff_cur) && (left_len>0) && ( ( read_len=SOCKETX_READ(chainX_req, buff_cur, left_len) ) > 0 ) )
					{
						//DBG_DB_LN("read_len: %d [%s]", read_len, buff_cur);
						read_pos += read_len;
						left_len -= read_len;
						if (left_len<=0)
						{
							break;
						}
						buff_cur += read_len;
					}

					if ( (chainX_req->serial_cb) && (read_pos>0) )
					{
						//DBG_DB_LN("(buff %d/%d: %s)", read_pos, nread, buff);
						chainX_req->serial_cb(chainX_req, buff, read_pos);
					}
					SAFE_FREE(buff);
				}
			}
		}
		else
		{
			DBG_ER_LN("select error !!! (result: %d, errno: %d %s)", result, errno, strerror(errno));
			break;
		}
	}

	chainX_status_set(chainX_req, 0);
	if (chainX_req->linked_cb) chainX_req->linked_cb(chainX_req);
	chainX_fdset_clear(chainX_req);
}
#endif

// for udp
void chainX_post_register(ChainXCtx_t *chainX_req, chainX_post_fn cb)
{
	if (chainX_req)
	{
		chainX_req->post_cb = cb;
	}
}

static void chainX_loop_post(ChainXCtx_t *chainX_req)
{
	if (chainX_req==NULL) return;

	if (chainX_req->linked_cb) chainX_req->linked_cb(chainX_req);

	while ( ( chainX_quit_check(chainX_req)== 0 ) && ( chainX_linked_check(chainX_req) == 0 ) &&
					((chainX_infinite_get(chainX_req)) || (chainX_recycle_get(chainX_req) > 0)) )
	{
		int result = 0;
		int nread = 0;

		chainX_fdset_clear(chainX_req);
		chainX_fdset_setall(chainX_req);

		if ( chainX_infinite_get(chainX_req) == 0 )
		{
			chainX_recycle_dec(chainX_req);
		}

		result = chainX_RW_select(chainX_req);
		if (result == -1)
		{
			if (errno==EINTR) continue;
			DBG_TR_LN("select error !!! (result: %d, errno: %d %s)", result, errno, strerror(errno));
			break;
		}
		else if (result==0)
		{
			// no isset
		}
		else if ( CHAINX_FD_ISSET_R(chainX_req) ) 
		{
			{
				//nread = LEN_OF_SSL_BUFFER;
				SAFE_IOCTL(chainX_fd_get(chainX_req), FIONREAD, &nread);
				SAFE_MEMSET(&chainX_req->addr_frm, 0, sizeof(struct sockaddr));
			}

			if (nread>0)
			{
				size_t read_pos = 0;
				int read_len = 0;
				size_t left_len = nread;
				char *buff = SAFE_CALLOC(1, nread+1);
				if (buff)
				{
					char *buff_cur = buff;

					while ( (buff_cur) && (left_len>0) && ( ( read_len=SOCKETX_RECV_FROM(chainX_req, buff_cur, left_len) ) > 0 ) )
					{
						//DBG_DB_LN("read_len: %d [%s]", read_len, buff_cur);
						//DBG_DB_LN("(read_len: %d, read_pos: %zd)", read_len, read_pos);
						read_pos += read_len;
						left_len -= read_len;
						if (left_len<=0)
						{
							break;
						}
						buff_cur += read_len;
					}

					if ( (chainX_req->post_cb) && (read_pos>0) )
					{
						//DBG_DB_LN("(buff %d/%d: %s)", read_pos, nread, buff);
						chainX_req->post_cb(chainX_req, buff, read_pos);
					}
					SAFE_FREE(buff);
				}
			}
		}
		else
		{
			DBG_ER_LN("select error !!! (result: %d, errno: %d %s)", result, errno, strerror(errno));
			break;
		}
	}

	DBG_TR_LN("out !!! (%s:%u, quit: %d, status: %d)", chainX_req->netinfo.addr.ipv4 , chainX_req->netinfo.port, chainX_quit_check(chainX_req), chainX_linked_check(chainX_req));

	chainX_status_set(chainX_req, 0);
	if (chainX_req->linked_cb) chainX_req->linked_cb(chainX_req);
	chainX_fdset_clear(chainX_req);
}

// for tcp client
void chainX_pipe_register(ChainXCtx_t *chainX_req, chainX_pipe_fn cb)
{
	chainX_req->pipe_cb = cb;
}

static void chainX_loop_pipe(ChainXCtx_t *chainX_req)
{
	if (chainX_req==NULL) return;

	if (chainX_req->linked_cb) chainX_req->linked_cb(chainX_req);

	while ( ( chainX_quit_check(chainX_req)== 0 ) && ( chainX_linked_check(chainX_req) == 0 ) )
	{
		int result = 0;
		int nread = 0;

		chainX_fdset_clear(chainX_req);
		chainX_fdset_setall(chainX_req);

		result = chainX_RW_select(chainX_req);
		if (result == -1)
		{
			if (errno==EINTR) continue;
			DBG_TR_LN("select error !!! (result: %d, errno: %d %s)", result, errno, strerror(errno));
			break;
		}
		else if (result==0)
		{
			// no isset
		}
		else
		{
			int so_error = chainX_socket_error(chainX_req);
			switch (so_error)
			{
				// GCC 32
				case EPIPE:
				// GCC 107, ? 128
				case ENOTCONN:
				// GCC 103, ? 113
				case ECONNABORTED:
				// GCC 113, ? 118
				case EHOSTUNREACH:
				// GCC 131
				case ECONNRESET:
				// GCC 145
				case ETIMEDOUT:
					DBG_ER_LN("select error !!! (result: %d, errno: %d %s, so_error: %d %s)", result, errno, strerror(errno), so_error, strerror(so_error));
					goto disconnected;
					break;
				// GCC 115, ? 119
				case EINPROGRESS:
				default:
					if ( CHAINX_FD_ISSET_E(chainX_req) )
					{
						DBG_ER_LN("FD_ISSET - fdeset");
						goto disconnected;
					}
					else if ( CHAINX_FD_ISSET_R(chainX_req) )
					{
						if (chainX_security_get(chainX_req) == 1 )
						{
							nread = LEN_OF_SSL_BUFFER;
						}
						else
						{
							SAFE_IOCTL(chainX_fd_get(chainX_req), FIONREAD, &nread);
							//DBG_TR_LN("(nread: %d)", nread);
						}

						if ( nread == 0 )
						{
							DBG_ER_LN("ioctl error !!! (nread: %d, errno: %d %s)", nread, errno, strerror(errno));
							goto disconnected;
						}
						else
						{
							size_t read_pos = 0;
							int read_len = 0;
							size_t left_len = nread;
							char *buff = SAFE_CALLOC(1, nread+1);
							if (buff)
							{
								char *buff_cur = buff;

								while ( (buff_cur) && (left_len>0) && ( ( read_len=SOCKETX_READ(chainX_req, buff_cur, left_len) ) > 0 ) )
								{
									//DBG_DB_LN("read_len: %d [%s]", read_len, buff_cur);
									read_pos += read_len;
									left_len -= read_len;
									if ((left_len<=0) && (chainX_req->noblock>0))
									{
										size_t new_len = read_pos + LEN_OF_SSL_BUFFER;
										//DBG_TR_LN("(left_len: %zd, new_len: %zd, nread: %d, read_pos: %zd)", left_len, new_len, nread, read_pos);
										char *new_buff = SAFE_REALLOC(buff, new_len + 1);
										if ( new_buff == NULL )
										{
											DBG_ER_LN("SAFE_REALLOC error !!! (size: %zd -> %zd)", read_pos, new_len);
											break;
										}
										buff = new_buff;
										left_len += LEN_OF_SSL_BUFFER;
									}

									buff_cur += read_len;
								}

								if ( (chainX_req->pipe_cb) && (read_pos>0) )
								{
									//DBG_DB_LN("(buff %d/%d: %s)", read_pos, nread, buff);
									chainX_req->pipe_cb(chainX_req, buff, read_pos);
								}

								SAFE_FREE(buff);
							}
						}
					}
					break;
			}
		}
	}

	DBG_TR_LN("out !!! (%s:%u, quit: %d, status: %d)", chainX_req->netinfo.addr.ipv4 , chainX_req->netinfo.port, chainX_quit_check(chainX_req), chainX_linked_check(chainX_req));

disconnected:
	chainX_status_set(chainX_req, 0);
	if (chainX_req->linked_cb) chainX_req->linked_cb(chainX_req);
	chainX_fdset_clear(chainX_req);
}

static void *chainX_thread_handler_tcp(void *arg)
{
	ChainXCtx_t *chainX_req = (ChainXCtx_t *)arg;

	if (chainX_req==NULL)
	{
		return NULL;
	}

	SAFE_THREAD_DETACH_EX(chainX_req);

	while (chainX_quit_check(chainX_req) == 0)
	{
		int net_last = 0;
		{
			chainX_close(chainX_req);

			if (  ( chainX_init(chainX_req) == 0) && ((chainX_security_get(chainX_req)==0) || (chainXssl_link(chainX_req)==0))  )
			{
				net_last = 1;
				DBG_IF_LN("tcp-client ok !!! (%s:%u, dbg: %d, net_status: %d, sockfd: %d, net_security: %d)", chainX_req->netinfo.addr.ipv4 , chainX_req->netinfo.port, dbg_lvl_get(), chainX_req->status, chainX_fd_get(chainX_req), chainX_security_get(chainX_req));
				chainX_loop_pipe(chainX_req);
			}
			DBG_WN_LN("tcp-client broken !!! (%s:%u, dbg: %d, net_last: %d)", chainX_req->netinfo.addr.ipv4 , chainX_req->netinfo.port, dbg_lvl_get(), net_last);
		}

		if (net_last == 0) chainX_wait(chainX_req);
	}
	DBG_TR_LN("exit (%s:%u)", chainX_req->netinfo.addr.ipv4 , chainX_req->netinfo.port);

	return NULL;
}

int chainX_multi_sender(ChainXCtx_t *chainX_req, char *buffer, int nbufs)
{
	int ret = 0;

	if (chainX_req == NULL)
	{
		DBG_ER_LN("chainX_req is NULL !!!");
		return -1;
	}

	chainX_addr_to_set(chainX_req, chainX_req->netinfo.addr.ipv4, chainX_req->netinfo.port);

	DBG_TR_LN("SOCKETX_SENDTO ... (%s:%u)", chainX_req->netinfo.addr.ipv4, chainX_req->netinfo.port);
	if ( SOCKETX_SENDTO(chainX_req, buffer, nbufs) < 0)
	{
		DBG_ER_LN("SOCKETX_SENDTO error !!!");
		ret = -1;
	}

	return ret;
}

int chainX_multi_sender_and_post(ChainXCtx_t *chainX_req, char *buffer, int nbufs)
{
	int ret = 0;

	if (chainX_req == NULL)
	{
		DBG_ER_LN("chainX_req is NULL !!!");
		return -1;
	}

	/* create what looks like an ordinary UDP socket */
	if ((chainX_req->sockfd=chainX_udp_socket()) < 0)
	{
		DBG_ER_LN("sockfd error !!!");
		return -1;
	}

	chainX_mutex_init(chainX_req);

	/* set up destination address */
	chainX_addr_to_set(chainX_req, chainX_req->netinfo.addr.ipv4, chainX_req->netinfo.port);

	DBG_TR_LN("SOCKETX_SENDTO ... (%s:%u)", chainX_req->netinfo.addr.ipv4, chainX_req->netinfo.port);
	if ( SOCKETX_SENDTO(chainX_req, buffer, nbufs) < 0)
	{
		DBG_ER_LN("SOCKETX_SENDTO error !!!");
		ret = -1;
	}

	chainX_status_set(chainX_req, 1);
	chainX_loop_post(chainX_req);

	chainX_close(chainX_req);
	return ret;
}

static void *chainX_thread_handler_udp(void *arg)
{
	ChainXCtx_t *chainX_req = (ChainXCtx_t *)arg;

	if (chainX_req==NULL)
	{
		return NULL;
	}

	SAFE_THREAD_DETACH_EX(chainX_req);

	chainX_status_set(chainX_req, 0);
	chainX_infinite_set(chainX_req, 1);
	chainX_recycle_set(chainX_req, 0);
	
	while (chainX_quit_check(chainX_req) == 0)
	{
		int net_last = 0;
		{
			chainX_close(chainX_req);

			if ( chainX_init(chainX_req) == 0)
			{
				net_last = 1;
				DBG_IF_LN("udp-bind ok !!! (%s:%u, dbg: %d, net_status: %d, sockfd: %d, net_security: %d)", chainX_req->netinfo.addr.ipv4 , chainX_req->netinfo.port, dbg_lvl_get(), chainX_req->status, chainX_fd_get(chainX_req), chainX_security_get(chainX_req));
				chainX_loop_post(chainX_req);
			}
			DBG_WN_LN("udp-bind broken !!! (%s:%u, dbg: %d)", chainX_req->netinfo.addr.ipv4 , chainX_req->netinfo.port, dbg_lvl_get());
			chainX_status_set(chainX_req, 0);
		}

		if (net_last == 0) chainX_wait(chainX_req);
	}

	DBG_TR_LN("exit (%s:%u)", chainX_req->netinfo.addr.ipv4 , chainX_req->netinfo.port);

	return NULL;
}

#ifdef UTIL_EX_TTY
static void *chainX_thread_handler_tty(void *arg)
{
	ChainXCtx_t *chainX_req = (ChainXCtx_t *)arg;

	if (chainX_req==NULL)
	{
		return NULL;
	}

	SAFE_THREAD_DETACH_EX(chainX_req);

	chainX_status_set(chainX_req, 0);
	chainX_infinite_set(chainX_req, 1);
	chainX_recycle_set(chainX_req, 0);
	
	while (chainX_quit_check(chainX_req) == 0)
	{
		int net_last = 0;
		{
			chainX_close(chainX_req);

			if ( chainX_init(chainX_req) == 0)
			{
				net_last = 1;
				DBG_IF_LN("tty ok !!! (%s, dbg: %d, tty_status: %d, sockfd: %d)", chainX_req->ttyinfo.ttyname, dbg_lvl_get(), chainX_req->status, chainX_fd_get(chainX_req));
				chainX_loop_serial(chainX_req);
			}
			DBG_WN_LN("tty broken !!! (ttyname: %s, dbg: %d)", chainX_req->ttyinfo.ttyname, dbg_lvl_get());
			chainX_status_set(chainX_req, 0);
		}

		if (net_last == 0) chainX_wait(chainX_req);
	}

	DBG_TR_LN("exit (ttyname: %s)", chainX_req->ttyinfo.ttyname);

	return NULL;
}
#endif

void chainX_thread_stop(ChainXCtx_t *chainX_req)
{
	if (chainX_req)
	{
		chainX_quit_set(chainX_req, 1);
		chainX_wakeup(chainX_req);
	}
}

void chainX_thread_close(ChainXCtx_t *chainX_req)
{
	if ( (chainX_req) && (chainX_req->isfree==0) )
	{
		chainX_req->isfree++;
		chainX_close(chainX_req);

		if (chainX_req->in_detach == 0)
		{
			SAFE_THREAD_JOIN_EX(chainX_req);
		}
	}
	DBG_TR_LN("exit (mode: %d)", chainX_req->mode);
}

int chainX_thread_init(ChainXCtx_t *chainX_req)
{
	if (chainX_req==NULL)
	{
		DBG_ER_LN("chainX_req is NULL !!!");
		return -1;
	}

	if (chainX_security_get(chainX_req) == 1)
	{
		chainXssl_init(chainX_req);
	}

	if ( chainX_mutex_init(chainX_req) == -1 )
	{
		DBG_ER_LN("thread_mutex error !!!");
		return -1;
	}

	if ( chainX_check(chainX_req) == -1 )
	{
		DBG_ER_LN("chainX_check error !!! (chainX_req->mode: %d)", chainX_req->mode);
		return -1;
	}

	switch (chainX_req->mode)
	{
		case CHAINX_MODE_ID_TCP_CLIENT:
			if (SAFE_THREAD_CREATE(chainX_req->tid, NULL, chainX_thread_handler_tcp, chainX_req) != 0)
			{
				DBG_ER_LN("SAFE_THREAD_CREATE error !!!");
				return -1;
			}
			break;
		case CHAINX_MODE_ID_UDP_SERVER:
		case CHAINX_MODE_ID_MULTI_RECEIVER:
			if (SAFE_THREAD_CREATE(chainX_req->tid, NULL, chainX_thread_handler_udp, chainX_req) != 0)
			{
				DBG_ER_LN("SAFE_THREAD_CREATE error !!!");
				return -1;
			}
			break;
#ifdef UTIL_EX_TTY
		case CHAINX_MODE_ID_TTY:
			if (SAFE_THREAD_CREATE(chainX_req->tid, NULL, chainX_thread_handler_tty, chainX_req) != 0)
			{
				DBG_ER_LN("SAFE_THREAD_CREATE error !!!");
				return -1;
			}
#endif
			break;
		default:
			DBG_ER_LN("%s", DBG_TXT_NO_SUPPORT);
			return -1;
			break;
	}

	return 0;
}

#include <netinet/ip_icmp.h> 
#define RECV_TIMEOUT      1 // seconds
#define DEFDATALEN        56

#ifndef ICMP_MINLEN
#define	ICMP_MINLEN       8
#endif

#define PING_PKT_S        (DEFDATALEN+ICMP_MINLEN)

#ifndef ICMP_ECHO_REPLY
#define ICMP_ECHO_REPLY   0
#endif
#ifndef ICMP_ECHO
#define ICMP_ECHO         8
#endif
#ifndef ICMP_DEST_UNREACH
#define ICMP_DEST_UNREACH 3
#endif

#if (0)
// ip_icmp.h
struct icmphdr
{
	u_int8_t type; /* message type */
	u_int8_t code; /* type sub-code */
	u_int16_t checksum;

	union
	{
		struct
		{
		u_int16_t id;
		u_int16_t sequence;
		} echo; /* echo datagram */

		u_int32_t gateway; /* gateway address */

		struct
		{
			u_int16_t __unused;
			u_int16_t mtu;
		} frag; /* path mtu discovery */
	} un;
};
#endif

static int chainX_icmp(ChainXCtx_t *chainX_req)
{
	int pingloop = chainX_req->recycle;
	int ttl_val = 64;

	int msg_seq = 0;
	int msg_received_count=0;

	struct timespec time_start, time_end, tfs, tfe;
	long double rtt_msec = 0, total_msec = 0;

	struct timeval tv_out;
	if (chainX_req->select_wait > 0)
	{
		tv_out.tv_sec = chainX_req->select_wait;
	}
	else
	{
		tv_out.tv_sec = RECV_TIMEOUT;
	}
	tv_out.tv_usec = 0;

	clock_gettime(CLOCK_MONOTONIC, &tfs);

	// set socket options at ip to TTL and value to 64,
	// change to what you want by setting ttl_val
	if (SAFE_SSETOPT(chainX_fd_get(chainX_req), IPPROTO_IP, IP_TTL, &ttl_val, sizeof(ttl_val)) != 0)
	{
		DBG_ER_LN("SAFE_SSETOPT IP_TTL error !!! (errno: %d %s)", errno, strerror(errno));
		return 0;
	}

	// setting timeout of recv setting
	SAFE_SSETOPT(chainX_fd_get(chainX_req), SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv_out, sizeof tv_out);

	if (chainX_req->verbose)
	{
		DBG_IF_LN("PING %s (%s): %d bytes of data", chainX_hostname_get(chainX_req), chainX_ip_get(chainX_req), PING_PKT_S);
	}

	// send icmp packet in an infinite loop
	while (pingloop--)
	{
		// flag is whether packet was sent or not
		int recv_flag=0;
		
		struct icmp *pkt; // sizeof(struct icmp): 28
		char packet[LEN_OF_BUF1024];

		//filling packet
		memset(packet, 0, sizeof(packet));

		pkt = (struct icmp *) packet;
		pkt->icmp_type = ICMP_ECHO;
		pkt->icmp_cksum = 0;
		pkt->icmp_seq = ++msg_seq;
		pkt->icmp_id = getpid();
		pkt->icmp_cksum = buf_cksum((unsigned short *) pkt, PING_PKT_S);

		//send packet
		clock_gettime(CLOCK_MONOTONIC, &time_start);
		if ( SOCKETX_SENDTO(chainX_req, pkt, PING_PKT_S) <= 0 )
		{
			if (chainX_req->verbose)
			{
				DBG_ER_LN("SOCKETX_SENDTO error !!!");
			}
		}
		else // if packet was not sent, don't receive
		{
			int rlen = 0;
			//receive packet
			SAFE_MEMSET(&chainX_req->addr_frm, 0, sizeof(struct sockaddr));

			memset(packet, 0, sizeof(packet));
			if ( (rlen=SOCKETX_RECV_FROM(chainX_req, packet, sizeof(packet))) <= 0 )
			{
				if (chainX_req->verbose)
				{
					DBG_ER_LN("SOCKETX_RECV_FROM error !!!");
				}
			}
			else
			{
				struct iphdr *iphdr = (struct iphdr*)packet;
				int hlen = (iphdr->ihl << 2);
				pkt = (struct icmp *) (packet + hlen); /* skip ip hdr */

				clock_gettime(CLOCK_MONOTONIC, &time_end);

				double timeElapsed = ((double)(time_end.tv_nsec - time_start.tv_nsec))/1000000.0;
				rtt_msec = (time_end.tv_sec- time_start.tv_sec) * 1000.0 + timeElapsed;

				if (chainX_req->verbose)
				{
					DBG_TR_LN("(type: %d, code: %d, icmp_seq: %d, icmp_id: %d/%d)", pkt->icmp_type, pkt->icmp_code, pkt->icmp_seq, pkt->icmp_id, getpid());
				}

				//DBG_ER_LN("(rlen: %d, hlen: %d, sizeof(struct icmp): %d)", rlen, hlen, sizeof(struct icmp));
				if ( ( pkt->icmp_id == getpid() ) && ( pkt->icmp_seq == msg_seq ) && ( rlen > (hlen+ICMP_MINLEN) ) ) // we need to check this !!!
				{
					switch (pkt->icmp_type)
					{
						case ICMP_ECHO_REPLY: // 0
							if (pkt->icmp_code==0)
							{
								recv_flag ++;
								msg_received_count++;
								if (chainX_req->verbose)
								{
									DBG_IF_LN("%d bytes from %s (%s): icmp_seq=%d ttl=%d time=%.02Lf ms",
														rlen - hlen,
														chainX_reversename_get(chainX_req),
														chainX_ip_get(chainX_req),
														pkt->icmp_seq,
														ttl_val,
														rtt_msec);
								}
							}
							break;
						case ICMP_ECHO: // 8
						case ICMP_DEST_UNREACH: // 3
						default:
							break;
					}
				}
			}

			if ( recv_flag == 0 )
			{
				if (chainX_req->verbose)
				{
					DBG_ER_LN("Packet received with ICMP error !!! (type: %d, code: %d, pingloop: %d)", pkt->icmp_type, pkt->icmp_code, pingloop);
				}
			}
		}
		if ((pingloop>1) && (chainX_req->retry_hold > 0)) sleep(chainX_req->retry_hold);
	}
	clock_gettime(CLOCK_MONOTONIC, &tfe);
	double timeElapsed = ((double)(tfe.tv_nsec - tfs.tv_nsec))/1000000.0;

	total_msec = (tfe.tv_sec-tfs.tv_sec)*1000.0 + timeElapsed;

	if (chainX_req->verbose)
	{
		DBG_IF_LN("--- %s ping statistics ---", chainX_hostname_get(chainX_req));
		DBG_IF_LN("%d packets transmitted, %d received, %.0f%% packet loss, time: %.0Lf ms.",
							msg_seq,
							msg_received_count,
							((msg_seq - msg_received_count)/msg_seq) * 100.0,
							total_msec);
	}

	return msg_received_count;
}

int chainX_ping(ChainXCtx_t *chainX_req)
{
	int ret = 0;

	if (chainX_req == NULL)
	{
		DBG_ER_LN("chainX_req is NULL !!!");
		return ret;
	}

	if ((chainX_req->sockfd=chainX_icmp_open()) < 0)
	{
		DBG_ER_LN("sockfd error !!!");
		return ret;
	}

	if ( ( strlen(chainX_ip_get(chainX_req)) > 0) || ( strlen(chainX_hostname_get(chainX_req)) > 0 ) )
	{
		int addr_set = -1;
		char *ip_addr = strlen(chainX_ip_get(chainX_req)) > 0 ? chainX_ip_get(chainX_req): chainX_hostname_get(chainX_req);

		addr_set = chainX_addr_to_set(chainX_req, ip_addr , chainX_port_get(chainX_req) );
		if ( addr_set == 0 )
		{
			// IP -> addr
		}
		else if (strlen(chainX_hostname_get(chainX_req)) > 0)
		{
			// Hostname -> addr
			ip_addr = chainX_ip_get(chainX_req);
			if ( ( chainX_nslookup( chainX_hostname_get(chainX_req), chainX_ip_get(chainX_req) ) == 0 ) &&	( chainX_nslookup_reverse(chainX_ip_get(chainX_req), chainX_reversename_get(chainX_req)) == 0 ) )
			{
				addr_set = chainX_addr_to_set(chainX_req, ip_addr , chainX_port_get(chainX_req) );
			}
		}

		if (addr_set == 0)
		{
			ret= chainX_icmp(chainX_req);
		}
	}

	SAFE_SCLOSE(chainX_req->sockfd);
	return ret;
}
