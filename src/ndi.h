#pragma once
#include <string>
#include <vector>

/// Finding NDI senders on the LAN, without going through DistroAV.
///
/// Asking a throwaway ndi_source for its property list crashed OBS: DistroAV's finder thread
/// signals the source that asked, and by then we had released it. So we talk to the NDI runtime
/// ourselves - the same library DistroAV uses, loaded from the process if it is already there -
/// and keep one finder alive for the session.
namespace kennelNdi {
/// NDI names of the senders seen so far, e.g. "TOWER (Kennel WARDOGS)".
/// waitMs: how long to wait for the first answer; later calls return what the finder has.
std::vector<std::string> sources(int waitMs = 1000);
/// Addresses to look at directly, as well as whatever NDI discovers by itself. NDI's discovery is
/// multicast and does not cross subnets or a firewall that blocks it; the squad mates we already
/// found by our own beacon can be handed to it by address instead.
void setExtraIps(const std::vector<std::string> &ips);
/// True if the NDI runtime could be loaded at all.
bool available();
/// Stops the finder. Called before OBS unloads its modules.
void shutdown();
} // namespace kennelNdi
