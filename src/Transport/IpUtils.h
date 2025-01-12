#pragma once
#include <uv.h>
#include <string>
#include <Base/base.h>
namespace Utils {
	class IpUtils {
	public:
		static int GetFamily(const std::string& ip)
		{
			if (ip.size() >= INET6_ADDRSTRLEN)
			{
				return AF_UNSPEC;
			}

			const auto* ipPtr = ip.c_str();
			char ipBuffer[INET6_ADDRSTRLEN] = { 0 };

			if (uv_inet_pton(AF_INET, ipPtr, ipBuffer) == 0)
			{
				return AF_INET;
			}
			else if (uv_inet_pton(AF_INET6, ipPtr, ipBuffer) == 0)
			{
				return AF_INET6;
			}
			else
			{
				return AF_UNSPEC;
			}
		}

		static void GetAddressInfo(const struct sockaddr* addr, int& family, std::string& ip, uint16_t& port)
		{
			char ipBuffer[INET6_ADDRSTRLEN] = { 0 };
			int err;

			switch (addr->sa_family)
			{
			case AF_INET:
			{
				err = uv_inet_ntop(
					AF_INET,
					std::addressof(reinterpret_cast<const struct sockaddr_in*>(addr)->sin_addr),
					ipBuffer,
					sizeof(ipBuffer));

				if (err)
				{
					LOG_DEBUG("IP","uv_inet_ntop() failed: " << uv_strerror(err));
				}

				port =
					static_cast<uint16_t>(ntohs(reinterpret_cast<const struct sockaddr_in*>(addr)->sin_port));

				break;
			}

			case AF_INET6:
			{
				err = uv_inet_ntop(
					AF_INET6,
					std::addressof(reinterpret_cast<const struct sockaddr_in6*>(addr)->sin6_addr),
					ipBuffer,
					sizeof(ipBuffer));

				if (err)
				{
					LOG_DEBUG("IP","uv_inet_ntop() failed: " << uv_strerror(err));
				}

				port =
					static_cast<uint16_t>(ntohs(reinterpret_cast<const struct sockaddr_in6*>(addr)->sin6_port));

				break;
			}

			default:
			{
				LOG_DEBUG("IP","unknown network family: " << static_cast<int>(addr->sa_family));
			}
			}

			family = addr->sa_family;
			ip.assign(ipBuffer);
		}

		static size_t GetAddressLen(const struct sockaddr* addr)
		{
			switch (addr->sa_family)
			{
			case AF_INET:
			{
				return sizeof(struct sockaddr_in);
			}

			case AF_INET6:
			{
				return sizeof(struct sockaddr_in6);
			}

			default:
			{
				LOG_DEBUG("IP","unknown network family: " << static_cast<int>(addr->sa_family));
			}
			}
		}

		static void NormalizeIp(std::string& ip)
		{

			sockaddr_storage addrStorage{};
			char ipBuffer[INET6_ADDRSTRLEN] = { 0 };
			int err;

			switch (GetFamily(ip))
			{
			case AF_INET:
			{
				err = uv_ip4_addr(ip.c_str(), 0, reinterpret_cast<struct sockaddr_in*>(&addrStorage));

				if (err != 0)
				{
					LOG_DEBUG("IP","uv_ip4_addr() failed: ip:"<<ip<<" error:" << uv_strerror(err));
				}

				err = uv_ip4_name(
					reinterpret_cast<const struct sockaddr_in*>(std::addressof(addrStorage)),
					ipBuffer,
					sizeof(ipBuffer));

				if (err != 0)
				{
					LOG_DEBUG("IP","uv_ip4_name() failed: ip:"<<ip<<" error:" << uv_strerror(err));
				}

				ip.assign(ipBuffer);

				break;
			}

			case AF_INET6:
			{
				err = uv_ip6_addr(ip.c_str(), 0, reinterpret_cast<struct sockaddr_in6*>(&addrStorage));

				if (err != 0)
				{
					LOG_DEBUG("IP","uv_ip6_addr() failed: ip:"<<ip<<" error:" << uv_strerror(err));
				}

				err = uv_ip6_name(
					reinterpret_cast<const struct sockaddr_in6*>(std::addressof(addrStorage)),
					ipBuffer,
					sizeof(ipBuffer));

				if (err != 0)
				{
					LOG_DEBUG("IP","uv_ip6_name() failed: ip:"<<ip<<" error:" << uv_strerror(err));
				}

				ip.assign(ipBuffer);

				break;
			}

			default:
			{
				LOG_DEBUG("IP","invalid IP '%s'", ip.c_str());
			}
			}
		}

	};

}