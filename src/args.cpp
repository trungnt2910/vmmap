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

#include <cctype>
#include <stdexcept>
#include <string>

#include "args.h"

VmmapArgs ParseArgs(int argc, char** argv)
{
	VmmapArgs vmmapArgs;
	for (int i = 1; i < argc; ++i)
	{
		std::string arg = argv[i];
		if (arg == "-w" || arg == "-wide")
		{
			vmmapArgs.wide = true;
		}
		else if (arg == "-v" || arg == "-verbose")
		{
			vmmapArgs.wide = true;
			vmmapArgs.submap = true;
			vmmapArgs.allSplitLibs = true;
			vmmapArgs.noCoalesce = true;
		}
		else if (arg == "-pages")
		{
			vmmapArgs.pages = true;
		}
		else if (arg == "-interleaved")
		{
			vmmapArgs.interleaved = true;
		}
		else if (arg == "-submap")
		{
			vmmapArgs.submap = true;
		}
		else if (arg == "allSplitLibs")
		{
			vmmapArgs.allSplitLibs = true;
		}
		else if (arg == "-summary")
		{
			vmmapArgs.summary = true;
		}
		else if (arg == "-stacks")
		{
			vmmapArgs.stacks = true;
			vmmapArgs.interleaved = true;
			vmmapArgs.noCoalesce = true;
		}
		else if (arg == "-fullStacks")
		{
			vmmapArgs.fullStacks = true;
			vmmapArgs.stacks = true;
			vmmapArgs.interleaved = true;
			vmmapArgs.noCoalesce = true;
		}
		else if (arg == "-forkCorpse")
		{
			vmmapArgs.forkCorpse = true;
		}
		else if (arg[0] != '-')
		{
			// To do: Support search by process name.
			for (char ch : arg)
			{
				if (!isdigit(ch))
				{
					throw std::runtime_error("[invalid usage]: Only PID is supported at the moment.");
				}
			}

			vmmapArgs.pid = std::stoi(arg);
		}
		else
		{
			throw std::invalid_argument("[invalid usage]: unrecognized option \'" + arg + "\'");
		}
	}

	if (vmmapArgs.pid == -1)
	{
		throw std::invalid_argument("[invalid usage]: no process specified");
	}

	return vmmapArgs;
}