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

#ifndef VMMAP_MAP_H__
#define VMMAP_MAP_H__

#include <cstring>
#include <cstdint>
#include <list>
#include <string>

struct VmmapEntry
{
	std::string regionType;
	std::intptr_t startAddress;
	std::intptr_t endAddress;

	std::size_t vsize;
	std::size_t rss;
	std::size_t dirty;
	std::size_t swap;

	std::size_t pageSize;

	std::string prt;
	std::string max;

	std::string shrmod;

	std::string purge;
	std::string regionDetail;

	inline bool IsMalloc() const
	{
		return strncmp(regionType.c_str(), "MALLOC", 6) == 0;
	}
};

struct VmmapSummaryEntry
{
	std::string regionType;

	std::size_t vsize = 0;
	std::size_t rss = 0;
	std::size_t dirty = 0;
	std::size_t swap = 0;

	std::size_t vol = 0;
	std::size_t nonvol = 0;
	std::size_t empty = 0;

	std::size_t regionCount = 0;

	inline bool IsMalloc() const
	{
		return strncmp(regionType.c_str(), "MALLOC", 6) == 0;
	}
};

enum
{
	READ_INDEX,
	WRITE_INDEX,
	EXECUTE_INDEX
};

std::list<VmmapEntry> Map(const VmmapArgs& args);

#endif