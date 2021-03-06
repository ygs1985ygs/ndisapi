/*************************************************************************/
/*              Copyright (c) 2000-2018 NT Kernel Resources.             */
/*                           All Rights Reserved.                        */
/*                          http://www.ntkernel.com                      */
/*                           ndisrd@ntkernel.com                         */
/*                                                                       */
/* Module Name:  dnstrace.cpp                                            */
/*                                                                       */
/* Abstract: Defines the entry point for the console application.        */
/*                                                                       */
/* Environment:                                                          */
/*   User mode                                                           */
/*                                                                       */
/*************************************************************************/

#include "stdafx.h"

static const unsigned short IPPORT_DNS = 53;

std::string get_type(uint16_t type)
{
	switch (type)
	{
	case 1:
		return std::string("A");
		break;
	case 2:
		return std::string("NS");
		break;
	case 5:
		return std::string("CNAME");
		break;
	case 6:
		return std::string("SOA");
		break;
	case 11:
		return std::string("WKS");
		break;
	case 12:
		return std::string("PTR");
		break;
	case 15:
		return std::string("MX");
		break;
	case 28:
		return std::string("AAAA");
		break;
	case 33:
		return std::string("SRV");
		break;
	case 255:
		return std::string("ANY");
		break;
	default:
		return std::string("UNKNOWN");
	}
}

size_t get_url_size(char data[])
{
	size_t i = 0;
	size_t toskip = data[0];

	// skip each set of chars until (0) at the end
	while (toskip != 0)
	{
		i += toskip + 1;
		toskip = data[i];
	}
	// return the length of the array including the (0) at the end
	return i + 1;
}

std::string get_url(char data[])
{
	size_t length = get_url_size(data) - 1;

	std::vector<char> url;
	url.reserve(length);

	size_t i = 0;
	size_t toread = data[0];
	size_t start = 0;
	i++;

	while (toread != 0)
	{
		if (start)
			url.push_back('.');

		// get everything bettween the dots
		for (; i <= start + toread; i++)
			url.push_back(data[i]);

		// next chunk
		toread = data[i];
		start = i;

		i++;
	}

	return std::string(url.cbegin(), url.cend());
}

void DumpDnsResponseData(dns_header_ptr pDnsHeader, uint16_t length)
{
	PDNS_RECORD dnsQueryResultPtr = NULL;

	PDNS_MESSAGE_BUFFER pDnsMessageBuffer = reinterpret_cast<PDNS_MESSAGE_BUFFER>(pDnsHeader);

	// Convert DNS header to host order
	DNS_BYTE_FLIP_HEADER_COUNTS(&pDnsMessageBuffer->MessageHead);

	std::cout << std::setw(16) << "id: " << pDnsMessageBuffer->MessageHead.Xid << std::endl;
	std::cout << std::setw(16) << "# questions: " << pDnsMessageBuffer->MessageHead.QuestionCount << std::endl;
	std::cout << std::setw(16) << "# answers: " << pDnsMessageBuffer->MessageHead.AnswerCount << std::endl;
	std::cout << std::setw(16) << "# ns: " << pDnsMessageBuffer->MessageHead.NameServerCount << std::endl;
	std::cout << std::setw(16) << "# ar: " << pDnsMessageBuffer->MessageHead.AdditionalCount << std::endl;

	uint16_t num_questions = pDnsMessageBuffer->MessageHead.QuestionCount;

	if (num_questions)
		std::cout << "QUESTIONS" << std::endl;

	char* pData = reinterpret_cast<char*>(pDnsHeader + 1);
	while (num_questions--)
	{
		qr_record_ptr qRecordPtr = reinterpret_cast<qr_record_ptr>(pData + get_url_size(pData));
		std::cout << std::setw(16) << "TYPE: " << get_type(ntohs(qRecordPtr->type)) << std::endl;
		std::cout << std::setw(16) << "CLASS: " << ntohs(qRecordPtr->clas) << std::endl;
		std::cout << std::setw(16) << "URL: " << get_url(pData) << std::endl;

		pData = reinterpret_cast<char*>(qRecordPtr + 1);
	}

	if (pDnsMessageBuffer->MessageHead.AnswerCount)
		std::cout << "ANSWERS" << std::endl;

	// Get DNS records from the DNS response
	DNS_STATUS dnsStatus = DnsExtractRecordsFromMessage_W(
		pDnsMessageBuffer,
		length,
		&dnsQueryResultPtr
	);

	// Revert changes in DNS header
	DNS_BYTE_FLIP_HEADER_COUNTS(&pDnsMessageBuffer->MessageHead);

	if (dnsStatus == 0)
	{
		PDNS_RECORD dnsRecordPtr = dnsQueryResultPtr;

		while (dnsRecordPtr)
		{
			INT itemIndex = MAXINT;
			ULONG ipAddrStringLength = INET6_ADDRSTRLEN;
			TCHAR ipAddrString[INET6_ADDRSTRLEN] = { '\0' };

			if (dnsRecordPtr->wType == DNS_TYPE_A)
			{
				net::ip_address_v4 ipv4Address;

				ipv4Address.s_addr = dnsRecordPtr->Data.A.IpAddress;

				std::cout << std::setw(16) << "A: " << std::string(ipv4Address) << std::endl;
			}
			else if (dnsRecordPtr->wType == DNS_TYPE_AAAA)
			{
				net::ip_address_v6 ipv6Address;

				memcpy_s(
					ipv6Address.s6_addr,
					sizeof(ipv6Address.s6_addr),
					dnsRecordPtr->Data.AAAA.Ip6Address.IP6Byte,
					sizeof(dnsRecordPtr->Data.AAAA.Ip6Address.IP6Byte)
				);

				std::cout << std::setw(16) << "AAAA: " << std::string(ipv6Address) << std::endl;
			}
			else if (dnsRecordPtr->wType == DNS_TYPE_CNAME)
			{
				std::wcout << std::setw(16) << L"CNAME: " << dnsRecordPtr->Data.CNAME.pNameHost << std::endl;
			}

			dnsRecordPtr = dnsRecordPtr->pNext;
		}

	}
	
	if (dnsQueryResultPtr)
	{
		DnsRecordListFree(dnsQueryResultPtr, DnsFreeRecordList);
	}
}

int main()
{
	auto pApi = std::make_unique<simple_packet_filter>(
		[](INTERMEDIATE_BUFFER& buffer) 
		{ 
			ether_header_ptr pEth = reinterpret_cast<ether_header_ptr>(buffer.m_IBuffer);

			if (ntohs(pEth->h_proto) == ETH_P_IP)
			{
				iphdr_ptr pIpHeader = reinterpret_cast<iphdr_ptr>( pEth + 1 );

				if (pIpHeader->ip_p == IPPROTO_UDP)
				{
					udphdr_ptr pUdpHeader = reinterpret_cast<udphdr_ptr>(((PUCHAR)pIpHeader) + sizeof(DWORD)*pIpHeader->ip_hl);
					if (ntohs(pUdpHeader->th_sport) == IPPORT_DNS)
					{
						std::cout << "IP HEADER" << std::endl;
						std::cout << std::setfill(' ') << std::setw(16) << "source : " << static_cast<unsigned>(pIpHeader->ip_src.S_un.S_un_b.s_b1) << "."
							<< static_cast<unsigned>(pIpHeader->ip_src.S_un.S_un_b.s_b2) << "."
							<< static_cast<unsigned>(pIpHeader->ip_src.S_un.S_un_b.s_b3) << "."
							<< static_cast<unsigned>(pIpHeader->ip_src.S_un.S_un_b.s_b4) << std::endl;
						std::cout << std::setw(16) << "dest : " << static_cast<unsigned>(pIpHeader->ip_dst.S_un.S_un_b.s_b1) << "."
							<< static_cast<unsigned>(pIpHeader->ip_dst.S_un.S_un_b.s_b2) << "."
							<< static_cast<unsigned>(pIpHeader->ip_dst.S_un.S_un_b.s_b3) << "."
							<< static_cast<unsigned>(pIpHeader->ip_dst.S_un.S_un_b.s_b4) << std::endl;
						std::cout << "UDP HEADER" << std::endl;
						std::cout << std::setw(16) << "source port : " << static_cast<unsigned>(ntohs(pUdpHeader->th_sport)) << std::endl;
						std::cout << std::setw(16) << "dest port : " << static_cast<unsigned>(ntohs(pUdpHeader->th_dport)) << std::endl;

						dns_header_ptr pDnsHeader = reinterpret_cast<dns_header_ptr>( pUdpHeader + 1 );

						std::cout << "DNS HEADER" << std::endl;

						DumpDnsResponseData(pDnsHeader, static_cast<uint16_t>(buffer.m_Length - (reinterpret_cast<char*>(pDnsHeader) - reinterpret_cast<char*>(pEth))));
						
						std::cout << std::setw(80) << std::setfill('-') << "-" << std::endl;
					}
				}
			}
			return PacketAction::pass;
		},
		nullptr
	);

	if (pApi->IsDriverLoaded())
	{
		std::cout << "WinpkFilter is loaded" << std::endl << std::endl;
	}
	else
	{
		std::cout << "WinpkFilter is not loaded" << std::endl << std::endl;
		return 1;
	}

	std::cout << "Available network interfaces:" << std::endl << std::endl;
	size_t index = 0;
	for (auto& e : pApi->get_interface_list())
	{
		std::cout << ++index << ")\t" << e << std::endl;
	}

	std::cout << std::endl << "Select interface to filter:";
	std::cin >> index;

	if (index > pApi->get_interface_list().size())
	{
		std::cout << "Wrong parameter was selected. Out of range." << std::endl;
		return 0;
	}

	pApi->start_filter(index - 1);

	std::cout << "Press any key to stop filtering" << std::endl;

	std::ignore = _getch();

	std::cout <<"Exiting..." << std::endl;

    return 0;
}

