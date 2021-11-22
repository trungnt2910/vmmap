// This file is a part of vmmap for Darling.

// Copyright (c) 2021 Trung Nguyen

// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:

// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.

// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

// To-Do: Clean up this file, so that the printing logic and the 
// system information fetching logic are separated.

#include <chrono>
#include <cmath>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <list>
#include <unordered_map>

#include <err.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <libproc.h>
#include <sys/ioctl.h>
#include <termios.h> 
#include <unistd.h>

#include <CoreFoundation/CoreFoundation.h>

#include "args.h"
#include "debug.h"
#include "map.h"
#include "print.h"

static void PrintOverview(const std::list<VmmapEntry>& entries, const VmmapArgs& args);
static void PrintCore(const std::list<VmmapEntry>& entries, const VmmapArgs& args);
static void PrintSummary(const std::list<VmmapEntry>& entries, const VmmapArgs& args);
static void PrintMalloc(const std::list<VmmapEntry>& entries, const VmmapArgs& args);

static std::string GetProcessName(int pid);


void PrintHelp()
{
	const int arg_width = 15;

	std::cout << "vmmap: Gives you an indication of the VM used by a process\n";
	std::cout << "Usage: vmmap [-wide] [-pages] [-interleaved] [-submap] [-allSplitLibs] [-noCoalesce] [-summary] [-stacks] [-forkCorpse] <pid | partial-process-name | memory-graph-file> [<address>]\n";
	std::cout << "\n";
#define PRINT_OPTION(name, description) std::cout << "\t" << std::left << std::setw(arg_width) << name << description << "\n";
	PRINT_OPTION("-w/-wide", "print wide output");
	PRINT_OPTION("-v/-verbose", "equivalent to -w -submap -allSplitLibs -noCoalesce");
	PRINT_OPTION("-pages", "print region sizes in page counts rather than kilobytes");
	PRINT_OPTION("-interleaved", "print all regions in order, rather than non-writable then writable");
	PRINT_OPTION("-submap", "print info about submaps");
	PRINT_OPTION("-allSplitLibs", "print info about all system split libraries, even those not loaded by this process");
	PRINT_OPTION("-noCoalesce", "do not coalesce adjacent identical regions (default is to coalesce for more concise output)");
	PRINT_OPTION("-summary", "only print overall summary, not individual regions");
    PRINT_OPTION("-stacks", "show region allocation backtraces if target process uses MallocStackLogging (implies -interleaved -noCoalesce)");
	PRINT_OPTION("-fullStacks", "show region allocation backtraces with one line per frame");
	PRINT_OPTION("-forkCorpse", "generate a corpse fork from process and run vmmap on it");
#undef PRINT_OPTION
}

void Print(const std::list<VmmapEntry>& entries, const VmmapArgs& args)
{
	if (args.forkCorpse)
	{
		throw new std::invalid_argument("vmmap: -forkCorpse not implemented");
	}

	PrintOverview(entries, args);

	if (!args.summary)
	{
		std::cout << "Virtual Memory Map of process " << args.pid << " (" << GetProcessName(args.pid) << ")\n";
		std::cout << "Output report format: 0.0\n";
		std::cout << "VM page size: " << entries.begin()->pageSize << " bytes\n";
		std::cout << std::endl;

		if (!args.interleaved)
		{
			std::list<VmmapEntry> nonWritable;
			std::list<VmmapEntry> writable;

			for (const auto & entry : entries)
			{
				if (entry.prt.find("w") != std::string::npos)
				{
					writable.push_back(entry);
				}
				else
				{
					nonWritable.push_back(entry);
				}
			}

			std::cout << "==== Non-writable regions for process " << args.pid << std::endl;
			PrintCore(nonWritable, args);
			std::cout << std::endl;

			std::cout << "==== Writable regions for process " << args.pid << std::endl;
			PrintCore(writable, args);
			std::cout << std::endl;
		}
		else
		{
			// To-Do.
			std::cout << "==== regions for processregions for process " << args.pid << "  (non-writable and writable regions are interleaved)" << std::endl;
			PrintCore(entries, args);
			std::cout << std::endl;
		}

		std::cout	<< "==== Legend\n"
					<< "SM=sharing mode:\n"
        			<< "\t\tCOW=copy_on_write PRV=private NUL=empty ALI=aliased\n"
        			<< "\t\tSHM=shared ZER=zero_filled S/A=shared_alias\n"
					<< "PURGE=purgeable mode:\n"
        			<< "\t\tV=volatile N=nonvolatile E=empty   otherwise is unpurgeable\n"
					<< std::endl;
	}

	PrintSummary(entries, args);
}

static std::string GetProcessName(int pid)
{
	proc_taskallinfo info;
	int size = sizeof(info);
	int result = proc_pidinfo(pid, PROC_PIDTASKALLINFO, 0, &info, size);

	if (result != size)
	{
		throw new std::invalid_argument("vmmap: proc_pidinfo failed");
	}

	return info.pbsd.pbi_comm;
}

static std::string GetProcessPath(int pid)
{
	char path[PROC_PIDPATHINFO_MAXSIZE];
	int pathLength = proc_pidpath(pid, path, sizeof(path));
	if (pathLength <= 0)
	{
		throw new std::invalid_argument("vmmap: failed to get process path for pid " + std::to_string(pid) + ".");
	}
	return std::string(path);
}

static std::string CFStringToString(CFStringRef cfString)
{
	if (cfString == nullptr)
	{
		return "";
	}

	CFIndex length = CFStringGetLength(cfString);
	CFIndex maxSize = CFStringGetMaximumSizeForEncoding(length, kCFStringEncodingUTF8);
	char* buffer = new char[maxSize];
	if (!CFStringGetCString(cfString, buffer, maxSize, kCFStringEncodingUTF8))
	{
		throw new std::invalid_argument("vmmap: failed to convert CFString to string.");
	}
	std::string result(buffer);
	delete[] buffer;
	return result;
}

static std::string GetMacOSInfo()
{
	// CFPriv.h, not directly accessible.
	// We don't wanna upload this to the App Store anyway, so this is acceptable.
	// As dyld never closes dlopened libraries, we can safely keep a static open handle.
	static void* handle = dlopen("/System/Library/Frameworks/CoreFoundation.framework/CoreFoundation", RTLD_LOCAL | RTLD_LAZY);
	static const auto _CFCopyServerVersionDictionary = (CFDictionaryRef(*)())dlsym(handle, "_CFCopyServerVersionDictionary"); 
	static const auto _CFCopySystemVersionDictionary = (CFDictionaryRef(*)())dlsym(handle, "_CFCopySystemVersionDictionary");
	static const auto _kCFSystemVersionProductNameKey = *(CFStringRef*)dlsym(handle, "_kCFSystemVersionProductNameKey");
	static const auto _kCFSystemVersionProductVersionKey = *(CFStringRef*)dlsym(handle, "_kCFSystemVersionProductVersionKey");
	static const auto _kCFSystemVersionBuildVersionKey = *(CFStringRef*)dlsym(handle, "_kCFSystemVersionBuildVersionKey");

	CFDictionaryRef dict = nullptr;

	dict = _CFCopyServerVersionDictionary();
	if (dict == nullptr)
		dict = _CFCopySystemVersionDictionary();
	if (dict == nullptr)
		return "";
	
	CFStringRef productName = (CFStringRef)CFDictionaryGetValue(dict, _kCFSystemVersionProductNameKey);
	CFStringRef productVersion = (CFStringRef)CFDictionaryGetValue(dict, _kCFSystemVersionProductVersionKey);
	CFStringRef buildVersion = (CFStringRef)CFDictionaryGetValue(dict, _kCFSystemVersionBuildVersionKey);

	std::string result = CFStringToString(productName) + " " + CFStringToString(productVersion) + " (" + CFStringToString(buildVersion) + ")";

	CFRelease(dict);

	return result;
}

static void PrintOverview(const std::list<VmmapEntry>& entries, const VmmapArgs& args)
{
	proc_taskallinfo info;
	int size = sizeof(info);
	int result = proc_pidinfo(args.pid, PROC_PIDTASKALLINFO, 0, &info, size);

	if (result != size)
	{
		throw new std::invalid_argument("vmmap: failed to get process info for pid " + std::to_string(args.pid) + ".");
	}

	char path[PROC_PIDPATHINFO_MAXSIZE];
	int pathLength = proc_pidpath(args.pid, path, sizeof(path));
	if (pathLength <= 0)
	{
		throw new std::invalid_argument("vmmap: failed to get process path for pid " + std::to_string(args.pid) + ".");
	}

	// I'm too tired, this is the quickest and dirtiest hack. Ever
	// To do this properly, we must find a way to convert Darling paths to Linux paths.
	// Basically: The first executable region that has the same suffix as the executable name.
	auto it = std::find_if(entries.begin(), entries.end(), [&](const VmmapEntry& entry) { return entry.regionType == "__TEXT" && (entry.regionDetail.find(path) + pathLength == entry.regionDetail.size()); });
	
	std::cout << std::left << std::setw(30) << "Process:" << info.pbsd.pbi_comm << " [" << info.pbsd.pbi_pid << "]" << std::endl;
	std::cout << std::left << std::setw(30) << "Path:" << path << std::endl;
	std::cout << std::left << std::setw(30) << "Load Address:";
	if (it != entries.end())
	{
		std::cout << std::hex << it->startAddress << std::dec << std::endl;
	}
	else
	{
		std::cout << "???" << std::endl;
	}
	std::cout << std::left << std::setw(30) << "Identifier:" << info.pbsd.pbi_comm << std::endl;
	std::cout << std::left << std::setw(30) << "Version:" << "???" << std::endl;
	// Currently a stub. Waiting for Darling to support proc_archinfo. 	
	std::cout << std::left << std::setw(30) << "Code Type:" << "???" << std::endl;
	std::cout << std::left << std::setw(30) << "Parent Process:" << GetProcessName(info.pbsd.pbi_ppid) << " [" << info.pbsd.pbi_ppid << "]" << std::endl;
	std::cout << std::endl;

    auto t = std::time(nullptr);
    auto tm = *std::localtime(&t);
	std::cout << std::left << std::setw(30) << "Date/Time:" << std::put_time(&tm, "%Y-%m-%d %H:%M:%S %Z") << std::endl;
	std::chrono::system_clock::time_point tp{std::chrono::seconds(info.pbsd.pbi_start_tvsec) + std::chrono::microseconds(info.pbsd.pbi_start_tvusec)};
	t = std::chrono::system_clock::to_time_t(tp);
	tm = *std::localtime(&t);
	std::cout << std::left << std::setw(30) << "Launch Time:" << std::put_time(&tm, "%Y-%m-%d %H:%M:%S %Z") << std::endl;
	std::cout << std::left << std::setw(30) << "OS Version:" << GetMacOSInfo() << std::endl;
	std::cout << std::left << std::setw(30) << "Report Version:" << 0 << std::endl;
	std::cout << std::left << std::setw(30) << "Analysis Tool:" << GetProcessPath(getpid()) << std::endl;
	std::cout << std::left << std::setw(30) << "Analysis Tool Version:" << __DATE__ << " " << __TIME__ << std::endl;
	std::cout << std::endl;

	std::cout << std::left << std::setw(30) << "Physical footprint:" << "???" << std::endl;
	std::cout << std::left << std::setw(30) << "Physical footprint (peak):" << "???" << std::endl;
	std::cout << "----" << std::endl;
	std::cout << std::endl;
}

inline std::string TruncateStringPrefix(const std::string& str, std::size_t maxLength)
{
	if (maxLength < 3)
	{
		return std::string(maxLength, '.');
	}

	if (str.size() <= maxLength)
	{
		return str;
	}

	std::size_t realLength = maxLength - 3;

	return "..." + str.substr(str.size() - realLength);
}

inline std::string TruncateStringSuffix(const std::string& str, std::size_t maxLength)
{
	if (maxLength < 3)
	{
		return std::string(maxLength, '.');
	}

	if (str.size() <= maxLength)
	{
		return str;
	}

	std::size_t realLength = maxLength - 3;
	return str.substr(0, realLength) + "...";
}

inline static std::string FormatData(std::intptr_t bytes, std::string sep = " ")
{
	// We allow more kilobytes here, because it seems to be the default
	// for the stock vmmap.
	if (bytes < 9999 * 1024)
	{
		return std::to_string(bytes / 1024) + sep + "K";
	}
	else if (bytes < 1024 * 1024 * 1024)
	{
		return std::to_string(bytes / (1024 * 1024)) + sep + "M";
	}
	else
	{
		return std::to_string(bytes / (1024 * 1024 * 1024)) + sep + "G";
	}
}

template <typename T>
inline static std::string Percent(const T& t1, const T& t2)
{
	return std::to_string((int)std::round((long double)t1 / t2 * 100)) + "%";
}

inline static std::string PagesOrKilobytes(std::size_t bytes, std::size_t pageSize, bool pages)
{
	if (pages)
	{
		return std::to_string(bytes / pageSize);
	}

	return FormatData(bytes);
}

static void PrintCore(const std::list<VmmapEntry>& entries, const VmmapArgs& args)
{
	const int REGION_TYPE_WIDTH = 24;
	const int START_ADDRESS_WIDTH = 12;
	const int END_ADDRESS_WIDTH = 12;
	const int VSIZE_WIDTH = 6;
	const int RSDNT_WIDTH = 7;
	const int DIRTY_WIDTH = 7;
	const int SWAP_WIDTH = 7;
	const int PRTMAX_WIDTH = 7;
	const int SHRMOD_WIDTH = 6;
	const int PURGE_WIDTH = 8;

	int REGION_DETAIL_WIDTH = -1;

	if (isatty(STDOUT_FILENO) && !args.wide)
	{
		struct winsize w;
		ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);
		int width = (w.ws_col);

		// Come on, nobody is gonna use a terminal _that_ small.
		REGION_DETAIL_WIDTH = std::max(0, width - REGION_TYPE_WIDTH - 1 - START_ADDRESS_WIDTH - 1 - END_ADDRESS_WIDTH - 1 - 1 - VSIZE_WIDTH - RSDNT_WIDTH - DIRTY_WIDTH - SWAP_WIDTH - 1 - 1 - PRTMAX_WIDTH - 1 - SHRMOD_WIDTH - 1 - PURGE_WIDTH - 1);
	}

	// Header:
	std::cout 	<< std::left << std::setw(REGION_TYPE_WIDTH) << "REGION TYPE" << " " // The space separated from the string is intended. 
				<< std::right << std::setw(START_ADDRESS_WIDTH) << "START " << "-"
				<< std::left << std::setw(END_ADDRESS_WIDTH) << " END" << " "
				<< "["
				<< std::right << std::setw(VSIZE_WIDTH) << "VSIZE" // No spacing between these guys.
				<< std::right << std::setw(RSDNT_WIDTH) << "RSDNT"
				<< std::right << std::setw(DIRTY_WIDTH) << "DIRTY"
				<< std::right << std::setw(SWAP_WIDTH) << "SWAP"
				<< "] "
				<< std::left << std::setw(PRTMAX_WIDTH) << "PRT/MAX" << " "
				<< std::left << std::setw(SHRMOD_WIDTH) << "SHRMOD" << " "
				<< std::left << std::setw(PURGE_WIDTH) << "PURGE" << " "
				<< std::left << "REGION DETAIL"
				<< std::endl;

	for (const auto& entry : entries)
	{
		std::cout 	<< std::left << std::setw(REGION_TYPE_WIDTH) << entry.regionType << " " // The space separated from the string is intended. 
					<< std::right << std::setw(START_ADDRESS_WIDTH) << std::hex << entry.startAddress << std::dec << "-"
					<< std::left << std::setw(END_ADDRESS_WIDTH) << std::hex << entry.endAddress << std::dec << " "
					<< "["
					<< std::right << std::setw(VSIZE_WIDTH) << PagesOrKilobytes(entry.vsize, entry.pageSize, args.pages) // No spacing between these guys.
					<< std::right << std::setw(RSDNT_WIDTH) << PagesOrKilobytes(entry.rss, entry.pageSize, args.pages)
					<< std::right << std::setw(DIRTY_WIDTH) << PagesOrKilobytes(entry.dirty, entry.pageSize, args.pages)
					<< std::right << std::setw(SWAP_WIDTH) << PagesOrKilobytes(entry.swap, entry.pageSize, args.pages)
					<< "] "
					<< std::left << std::setw(PRTMAX_WIDTH) << entry.prt + "/" + entry.max << " "
					<< std::left << std::setw(SHRMOD_WIDTH) << entry.shrmod << " "
					<< std::left << std::setw(PURGE_WIDTH) << entry.purge << " "
					<< std::left << TruncateStringPrefix(entry.regionDetail, REGION_DETAIL_WIDTH)
					<< std::endl;
	}
}

static void PrintSummary(const std::list<VmmapEntry>& entries, const VmmapArgs& args)
{
	std::cout << "==== Summary for process " << args.pid << std::endl;
	intptr_t readOnlyTotal = 0;
	intptr_t readOnlyRss = 0;

	intptr_t writeTotal = 0;
	intptr_t writeRss = 0;
	intptr_t writeSwap = 0;

	// ReadOnly portion of Libraries: Total=736.8M resident=105.0M(14%) swapped_out_or_unallocated=631.8M(86%)
	// Writable regions: Total=44.6M written=0K(0%) resident=2100K(5%) swapped_out=0K(0%) unallocated=42.5M(95%)
	for (const auto & entry : entries)
	{
		if (entry.prt[WRITE_INDEX] == 'w')
		{
			writeTotal += entry.vsize;
			writeRss += entry.rss;
			writeSwap += entry.swap;
		}
		// ReadOnly portion of **Libraries** only.
		else if (entry.regionType == "__TEXT")
		{
			readOnlyTotal += entry.vsize;
			readOnlyRss += entry.rss;
		}
	}

	// I don't know if this is correct, but apparently this formula yields correct results in many cases.
	std::cout 	<< "ReadOnly portion of Libraries: "
				<< "Total=" << FormatData(readOnlyTotal, "") << " " 
				<< "resident=" << FormatData(readOnlyRss, "") << "(" << Percent(readOnlyRss, readOnlyTotal) << ") "
				<< "swapped_out_or_unallocated=" << FormatData(readOnlyTotal - readOnlyRss, "") << "(" << Percent(readOnlyTotal - readOnlyRss, readOnlyTotal) << ")"
				<< std::endl;

	std::cout 	<< "Writable regions: "
				<< "Total=" << FormatData(writeTotal, "") << " " 
				<< "written=" << FormatData(writeSwap, "") << "(" << Percent(writeSwap, writeTotal) << ") "
				<< "resident=" << FormatData(writeRss, "") << "(" << Percent(writeRss, writeTotal) << ") "
				<< "swapped_out=" << FormatData(writeSwap, "") << "(" << Percent(writeSwap, writeTotal) << ") "
				<< "unallocated=" << FormatData(writeTotal - writeRss - writeSwap, "") << "(" << Percent(writeTotal - writeRss - writeSwap, writeTotal) << ")"
				<< std::endl;

	std::cout << std::endl;

	std::string PagesOrSize = (args.pages) ? "PAGES" : "SIZE";
	const int REGION_TYPE_WIDTH = 30;
	const int VIRTUAL_WIDTH = 8;
	const int RESIDENT_WIDTH = 8;
	const int DIRTY_WIDTH = 8;
	const int SWAPPED_WIDTH = 8;
	const int VOLATILE_WIDTH = 8;
	const int NONVOL_WIDTH = 8;
	const int EMPTY_WIDTH = 8;
	const int REGION_COUNT_WIDTH = 7;

	// First line.
	std::cout   << std::left << std::setw(REGION_TYPE_WIDTH) << "" << " "
				<< std::right << std::setw(VIRTUAL_WIDTH) << "VIRTUAL" << " "
				<< std::right << std::setw(RESIDENT_WIDTH) << "RESIDENT" << " "
				<< std::right << std::setw(DIRTY_WIDTH) << "DIRTY" << " "
				<< std::right << std::setw(SWAPPED_WIDTH) << "SWAPPED" << " "
				<< std::right << std::setw(VOLATILE_WIDTH) << "VOLATILE" << " "
				<< std::right << std::setw(NONVOL_WIDTH) << "NONVOL" << " "
				<< std::right << std::setw(EMPTY_WIDTH) << "EMPTY" << " "
				<< std::right << std::setw(REGION_COUNT_WIDTH) << "REGION"
				<< std::endl;

	// Second line.
	std::cout   << std::left << std::setw(REGION_TYPE_WIDTH) << "REGION TYPE" << " "
				<< std::right << std::setw(VIRTUAL_WIDTH) << PagesOrSize << " "
				<< std::right << std::setw(RESIDENT_WIDTH) << PagesOrSize << " "
				<< std::right << std::setw(DIRTY_WIDTH) << PagesOrSize << " "
				<< std::right << std::setw(SWAPPED_WIDTH) << PagesOrSize << " "
				<< std::right << std::setw(VOLATILE_WIDTH) << PagesOrSize << " "
				<< std::right << std::setw(NONVOL_WIDTH) << PagesOrSize << " "
				<< std::right << std::setw(EMPTY_WIDTH) << PagesOrSize << " "
				<< std::right << std::setw(REGION_COUNT_WIDTH) << "COUNT" << " "
				<< "(non-coalesced)"
				<< std::endl;

	// Third line.
	std::cout   << std::left << std::setw(REGION_TYPE_WIDTH) << "===========" << " "
				<< std::right << std::setw(VIRTUAL_WIDTH) << "=======" << " "
				<< std::right << std::setw(RESIDENT_WIDTH) << "=======" << " "
				<< std::right << std::setw(DIRTY_WIDTH) << "=====" << " "
				<< std::right << std::setw(SWAPPED_WIDTH) << "=======" << " "
				<< std::right << std::setw(VOLATILE_WIDTH) << "========" << " "
				<< std::right << std::setw(NONVOL_WIDTH) << "======" << " "
				<< std::right << std::setw(EMPTY_WIDTH) << "=====" << " "
				<< std::right << std::setw(REGION_COUNT_WIDTH) << "======="
				<< std::endl;

	std::unordered_map<std::string, VmmapSummaryEntry> regions;

	for (const auto & entry : entries)
	{
		VmmapSummaryEntry& currentRegion = regions[entry.regionType];
		
		currentRegion.regionType = entry.regionType;
		currentRegion.vsize += entry.vsize;
		currentRegion.rss += entry.rss;
		currentRegion.dirty += entry.dirty;
		currentRegion.swap += entry.swap;
		
		if (entry.purge == "V")
		{
			currentRegion.vol += entry.vsize;
		}
		if (entry.purge == "N")
		{
			currentRegion.nonvol += entry.vsize;
		}
		if (entry.purge == "E")
		{
			currentRegion.empty += entry.vsize;
		}

		++currentRegion.regionCount;
	}

	std::size_t pageSize = entries.front().pageSize;

	for (const auto & kvp : regions)
	{
		const VmmapSummaryEntry& entry = kvp.second;
		std::cout	<< std::left << std::setw(REGION_TYPE_WIDTH) << TruncateStringSuffix(entry.regionType, REGION_TYPE_WIDTH) << " "
					<< std::right << std::setw(VIRTUAL_WIDTH) << PagesOrKilobytes(entry.vsize, pageSize, args.pages) << " "
					<< std::right << std::setw(RESIDENT_WIDTH) << PagesOrKilobytes(entry.rss, pageSize, args.pages) << " "
					<< std::right << std::setw(DIRTY_WIDTH) << PagesOrKilobytes(entry.dirty, pageSize, args.pages) << " "
					<< std::right << std::setw(SWAPPED_WIDTH) << PagesOrKilobytes(entry.swap, pageSize, args.pages) << " "
					<< std::right << std::setw(VOLATILE_WIDTH) << PagesOrKilobytes(entry.vol, pageSize, args.pages) << " "
					<< std::right << std::setw(NONVOL_WIDTH) << PagesOrKilobytes(entry.nonvol, pageSize, args.pages) << " "
					<< std::right << std::setw(EMPTY_WIDTH) << PagesOrKilobytes(entry.empty, pageSize, args.pages) << " "
					<< std::right << std::setw(REGION_COUNT_WIDTH) << entry.regionCount << " ";

		if (entry.IsMalloc())
		{
			std::cout << "see MALLOC ZONE table below";
		}

		std::cout << std::endl;
	}

	std::cout << std::endl;

	// Let's make this a separate function, as it may have conflicting column width variables.
	PrintMalloc(entries, args);
}

static void PrintMalloc(const std::list<VmmapEntry>& entries, const VmmapArgs& args)
{
	std::string PagesOrSize = (args.pages) ? "PAGES" : "SIZE";
	const int REGION_TYPE_WIDTH = 29;
	const int VIRTUAL_WIDTH = 10;
	const int RESIDENT_WIDTH = 10;
	const int DIRTY_WIDTH = 10;
	const int SWAPPED_WIDTH = 10;
	const int ALLOCATION_COUNT_WIDTH = 10;
	const int BYTES_ALLOCATED_WIDTH = 10;
	const int DIRTY_SWAP_FRAG_SIZE_WIDTH = 10;
	const int FRAG_WIDTH = 7;
	const int REGION_COUNT_WIDTH = 7;

	// First line.
	std::cout   << std::left << std::setw(REGION_TYPE_WIDTH) << "" << " "
				<< std::right << std::setw(VIRTUAL_WIDTH) << "VIRTUAL" << " "
				<< std::right << std::setw(RESIDENT_WIDTH) << "RESIDENT" << " "
				<< std::right << std::setw(DIRTY_WIDTH) << "DIRTY" << " "
				<< std::right << std::setw(SWAPPED_WIDTH) << "SWAPPED" << " "
				<< std::right << std::setw(ALLOCATION_COUNT_WIDTH) << "ALLOCATION" << " "
				<< std::right << std::setw(BYTES_ALLOCATED_WIDTH) << "BYTES" << " "
				<< std::right << std::setw(DIRTY_SWAP_FRAG_SIZE_WIDTH) << "DIRTY+SWAP" << " "
				<< std::right << std::setw(FRAG_WIDTH) << "" << " "
				<< std::right << std::setw(REGION_COUNT_WIDTH) << "REGION"
				<< std::endl;

	// Second line.
	std::cout   << std::left << std::setw(REGION_TYPE_WIDTH) << "MALLOC ZONE" << " "
				<< std::right << std::setw(VIRTUAL_WIDTH) << PagesOrSize << " "
				<< std::right << std::setw(RESIDENT_WIDTH) << PagesOrSize << " "
				<< std::right << std::setw(DIRTY_WIDTH) << PagesOrSize << " "
				<< std::right << std::setw(SWAPPED_WIDTH) << PagesOrSize << " "
				<< std::right << std::setw(ALLOCATION_COUNT_WIDTH) << "COUNT" << " "
				<< std::right << std::setw(BYTES_ALLOCATED_WIDTH) << "ALLOCATED" << " "
				<< std::right << std::setw(DIRTY_SWAP_FRAG_SIZE_WIDTH) << "FRAG SIZE" << " "
				<< std::right << std::setw(FRAG_WIDTH) << "% FRAG" << " "
				<< std::right << std::setw(REGION_COUNT_WIDTH) << "COUNT" << " "
				<< std::endl;

	// Third line.
	std::cout   << std::left << std::setw(REGION_TYPE_WIDTH) << "===========" << " "
				<< std::right << std::setw(VIRTUAL_WIDTH) << "=======" << " "
				<< std::right << std::setw(RESIDENT_WIDTH) << "=========" << " "
				<< std::right << std::setw(DIRTY_WIDTH) << "=========" << " "
				<< std::right << std::setw(SWAPPED_WIDTH) << "=========" << " "
				<< std::right << std::setw(ALLOCATION_COUNT_WIDTH) << "=========" << " "
				<< std::right << std::setw(BYTES_ALLOCATED_WIDTH) << "=========" << " "
				<< std::right << std::setw(DIRTY_SWAP_FRAG_SIZE_WIDTH) << "=========" << " "
				<< std::right << std::setw(FRAG_WIDTH) << "======" << " "
				<< std::right << std::setw(REGION_COUNT_WIDTH) << "======"
				<< std::endl;

	// Here, we might have to access the relevant APIs and fetch additional data. Let's ignore for now.

	std::unordered_map<std::string, VmmapSummaryEntry> mallocZones;

	for (const auto& entry : entries)
	{
		if (entry.IsMalloc())
		{
			VmmapSummaryEntry& summaryEntry = mallocZones[entry.regionDetail];
			summaryEntry.regionType = entry.regionDetail;
			summaryEntry.vsize += entry.vsize;
			summaryEntry.rss += entry.rss;
			summaryEntry.dirty += entry.dirty;
			summaryEntry.swap += entry.swap;
			
			++summaryEntry.regionCount;
		}
	}

	std::size_t pageSize = entries.front().pageSize;

	for (const auto & kvp : mallocZones)
	{
		const VmmapSummaryEntry& zone = kvp.second;
		std::cout   << std::left << std::setw(REGION_TYPE_WIDTH) << TruncateStringSuffix(zone.regionType, REGION_TYPE_WIDTH) << " "
					<< std::right << std::setw(VIRTUAL_WIDTH) << PagesOrKilobytes(zone.vsize, pageSize, args.pages) << " "
					<< std::right << std::setw(RESIDENT_WIDTH) << PagesOrKilobytes(zone.rss, pageSize, args.pages) << " "
					<< std::right << std::setw(DIRTY_WIDTH) << PagesOrKilobytes(zone.dirty, pageSize, args.pages) << " "
					<< std::right << std::setw(SWAPPED_WIDTH) << PagesOrKilobytes(zone.swap, pageSize, args.pages) << " "
					<< std::right << std::setw(ALLOCATION_COUNT_WIDTH) << "???" << " "
					<< std::right << std::setw(BYTES_ALLOCATED_WIDTH) << "???" << " "
					<< std::right << std::setw(DIRTY_SWAP_FRAG_SIZE_WIDTH) << "???" << " "
					<< std::right << std::setw(FRAG_WIDTH) << "??%" << " "
					<< std::right << std::setw(REGION_COUNT_WIDTH) << zone.regionCount << " "
					<< std::endl;
	}

	std::cout << std::endl;
}