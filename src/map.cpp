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

#include <cstdint>
#include <fstream>
#include <list>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include <unistd.h>

#include "args.h"
#include "debug.h"
#include "map.h"

// This current implementation is intended to be used for 
// Darling only, because of two reasons:
// 1. Darling's mach_vm_region family of APIs are incomplete.
// 2. These APIs are more complicated to implement then simply parse Linux's procfs.

// To update this program with a implementation based on Mach calls, 
// only this function will need to be replaced.

struct LinuxEntry
{
	intptr_t start;
	intptr_t end;

	std::string permissions;
	intptr_t offset;
	std::string dev;
	std::string inode;

	std::string description;

	std::unordered_map<std::string, std::string> tags;
};

static VmmapEntry LinuxToVmmap(const LinuxEntry& entry);
static void BadPid(int pid);
static void BadPerm(int pid);

const std::string SystemPrefix = "/Volumes/SystemRoot";
std::list<VmmapEntry> Map(const VmmapArgs& args)
{
	// Differentiate between non-existent pid and insufficient permissions.
	getpgid(args.pid);

	if (errno == ESRCH || errno == EINVAL)
	{
		BadPid(args.pid);
	}

	if (errno == EPERM)
	{
		BadPerm(args.pid);
	}

	std::list<VmmapEntry> entries;

	std::string fileName = "/proc/";
	fileName += std::to_string(args.pid);

	std::string smapsName = fileName + "/smaps";
	std::string mapsName = fileName + "/maps";

	std::ifstream proc_maps(smapsName);
	if (!proc_maps.is_open())
	{
		// smaps is not always present.
		// Fallback to maps, although we'll lose quite a lot of information.
		DEBUG_PRINT("Failed to open smaps.");
		proc_maps.open(mapsName);
		if (!proc_maps.is_open())
		{
			BadPid(args.pid);
		}
	}

	LinuxEntry currentLinuxEntry;

	const std::regex regex("([0-9a-fA-F]*)-([0-9a-fA-F]*)\\s*([rwxsp-]*)\\s*([0-9a-fA-F]*)\\s*([0-9a-fA-F]*):([0-9a-fA-F]*)\\s*([0-9a-fA-F]*)\\s*([\\S\\s]*)");

	bool firstLine = true;
	while (!proc_maps.eof())
	{
		static std::string line;
		std::getline(proc_maps, line);
    	std::smatch results;

		// Start of a new entry:
		if (std::regex_match(line, results, regex))
		{
			if (firstLine)
			{
				// This is the first entry, no entry to add.
				firstLine = false;
			}
			else
			{
				entries.push_back(LinuxToVmmap(currentLinuxEntry));
			}

			currentLinuxEntry.start = std::stoull(results[1], nullptr, 16);
			currentLinuxEntry.end = std::stoull(results[2], nullptr, 16);

			currentLinuxEntry.permissions = results[3];
			currentLinuxEntry.offset = std::stoull(results[4], nullptr, 16);
			currentLinuxEntry.dev = results[5].str() + ":" + results[6].str();
			currentLinuxEntry.inode = results[7];
			currentLinuxEntry.description = results[8];
		}
		// This is probably more details, provided by the smaps file.
		else
		{
			std::size_t splitIndex = line.find_first_of(":");

			if (splitIndex != std::string::npos)
			{	
				std::string name = line.substr(0, splitIndex);
				std::string value = line.substr(line.find_first_not_of(" ", splitIndex + 1));

				currentLinuxEntry.tags[name] = value;
			}
		}
	}

	std::unordered_set<std::string> executeableFiles;

	for (auto & entry : entries)
	{
		if (entry.regionType == "mapped file")
		{
			entry.regionDetail = SystemPrefix + entry.regionDetail;
			if (entry.prt[EXECUTE_INDEX] == 'x')
			{
				executeableFiles.insert(entry.regionDetail);
			}
		}
	}

	for (auto & entry : entries)
	{
		if (entry.regionType == "mapped file")
		{
			if (executeableFiles.count(entry.regionDetail))
			{
				if (entry.prt[EXECUTE_INDEX] == 'x')
				{
					entry.regionType = "__TEXT";
				}
				else
				{
					entry.regionType = "__DATA";
				}
			}
		}
	}
	
	return entries;
}

static void BadPerm(int pid)
{
	throw std::invalid_argument("vmmap: vmmap cannot examine process " + std::to_string(pid) + " because it no longer appears to be running.");
}

static void BadPid(int pid)
{
	throw std::invalid_argument("vmmap: vmmap cannot examine process " + std::to_string(pid) + " because you do not have appropriate privileges to examine it; try running with `sudo`.");
}

static size_t ParseSize(std::string size)
{
	std::stringstream ss(size);
	size_t num;
	std::string unit;
	ss >> num >> unit;
	
	if (unit == "kB")
	{
		return num * 1024;
	}
	else if (unit == "MB")
	{
		return num * 1024 * 1024;
	}
	else if (unit == "GB")
	{
		return num * 1024 * 1024 * 1024;
	}
	else
	{
		throw std::invalid_argument("vmmap: Failed to parse size: " + size);
	}
}

static std::unordered_set<std::string> MakeTagSet(const std::string& str)
{
	std::stringstream ss(str);
	std::unordered_set<std::string> tags;

	while (!ss.eof())
	{
		std::string tag;
		ss >> tag;
		tags.insert(tag);
	}

	return tags;
}

static VmmapEntry LinuxToVmmap(const LinuxEntry& entry)
{
	VmmapEntry vmmapEntry;

	// Numbers. They are the easiest.
	vmmapEntry.startAddress = entry.start;
	vmmapEntry.endAddress = entry.end;

	if (entry.tags.count("KernelPageSize"))
	{
		vmmapEntry.pageSize = ParseSize(entry.tags.at("KernelPageSize"));
	}
	else
	{
		vmmapEntry.pageSize = sysconf(_SC_PAGESIZE);
	}

	if (entry.tags.count("Size"))
	{
		vmmapEntry.vsize = ParseSize(entry.tags.at("Size"));
	}
	else
	{
		vmmapEntry.vsize = vmmapEntry.endAddress - vmmapEntry.startAddress;
	}

	if (entry.tags.count("Rss"))
	{
		vmmapEntry.rss = ParseSize(entry.tags.at("Rss"));
	}
	else
	{
		vmmapEntry.rss = vmmapEntry.vsize;
	}

	vmmapEntry.dirty = 0;
	
	if (entry.tags.count("Shared_Dirty"))
	{
		vmmapEntry.dirty = ParseSize(entry.tags.at("Shared_Dirty"));
	}
	if (entry.tags.count("Private_Dirty"))
	{
		vmmapEntry.dirty += ParseSize(entry.tags.at("Private_Dirty"));
	}

	vmmapEntry.swap = 0;
	if (entry.tags.count("Swap"))
	{
		vmmapEntry.swap = ParseSize(entry.tags.at("Swap"));
	}

	// Now to the protection.
	// There are two sources: Normal permissions, and the "VmFlags" tag.

	vmmapEntry.prt = "???";
	// We can trust these values.
	if (entry.permissions != "")
	{
		vmmapEntry.prt = entry.permissions.substr(0, 3);
	}

	vmmapEntry.max = "???";
	std::unordered_set<std::string> flags;
	// Now check the tags.
	if (entry.tags.count("VmFlags"))
	{
		vmmapEntry.max = "---";
		flags = MakeTagSet(entry.tags.at("VmFlags"));

		// mr  - may read
        // mw  - may write
        // me  - may execute
        // ms  - may share
		if (flags.count("mr"))
		{
			vmmapEntry.max[READ_INDEX] = 'r';
		}
		if (flags.count("mw"))
		{
			vmmapEntry.max[WRITE_INDEX] = 'w';
		}
		if (flags.count("me"))
		{
			vmmapEntry.max[EXECUTE_INDEX] = 'x';
		}
	}

	// Sharing mode
	vmmapEntry.shrmod = "NUL";

	// Purge
	// I don't know what this is? It is usually empty on my Mac.
	vmmapEntry.purge = "";

	// Region description.
	vmmapEntry.regionDetail = entry.description;

	// Region type.
	// Most of the time, it's VM_ALLOCATE.
	vmmapEntry.regionType = "VM_ALLOCATE";
	
	// Some kind of file. Let's see.
	if (entry.description.find("/") != std::string::npos)
	{
		vmmapEntry.regionType = "mapped file";
	}
	else if (entry.description == "HEAP")
	{
		vmmapEntry.regionType = "MALLOC";
	}
	else if (entry.description == "[stack]")
	{
		vmmapEntry.regionType = "Stack";
	}
	else if (entry.description.substr(0, 7) == "[stack:")
	{
		int id = std::stoi(entry.description.substr(7));
		vmmapEntry.regionType = "Stack";
		vmmapEntry.regionDetail = "thread " + std::to_string(id);
	}

	return vmmapEntry;
}