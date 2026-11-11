/**
 * vim: set ts=4 :
 * =============================================================================
 * Safe reads of server module memory, used to verify patch sites before patching.
 * =============================================================================
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License, version 3.0, as published by the
 * Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#ifndef __MODULEMEMORY_H__
#define __MODULEMEMORY_H__

#include <limits>
#if defined (_LINUX)
#include <dlfcn.h>
#include <errno.h>
#include <sys/uio.h>
#include <unistd.h>
#endif
#include "thirdparty/memutils.h"

// [address, address + length) lies in the range GetLibraryInfo reported (the code segment on Linux).
static inline bool IsInModule(const void* address, size_t length, const DynLibInfo& lib)
{
	uintptr_t startAddress = reinterpret_cast<uintptr_t>(address);
	uintptr_t baseAddress = reinterpret_cast<uintptr_t>(lib.baseAddress);
	return startAddress >= baseAddress && length <= lib.memorySize
		&& startAddress - baseAddress <= lib.memorySize - length;
}

#if defined (_LINUX)
// The kernel copies from 'source' into a pipe and reports EFAULT for unreadable memory instead of
// crashing. Chunks of PIPE_BUF bytes always fit, even into a pipe shrunk to a single page.
static inline bool ReadMemoryThroughPipe(void* destination, const void* source, size_t length)
{
	int fds[2];
	if (pipe(fds) != 0)
	{
		return false;
	}

	bool ok = true;
	for (size_t done = 0; ok && done < length;)
	{
		size_t chunk = length - done < 4096 ? length - done : 4096;
		ok = write(fds[1], static_cast<const BYTE*>(source) + done, chunk) == (ssize_t)chunk
			&& read(fds[0], static_cast<BYTE*>(destination) + done, chunk) == (ssize_t)chunk;
		done += chunk;
	}

	close(fds[0]);
	close(fds[1]);
	return ok;
}
#endif

// Read through the OS so stale operands fail without dereferencing inaccessible memory.
// Linux data segments can lie outside the executable range reported by GetLibraryInfo.
static inline bool ReadModuleMemory(void* destination, const void* source, size_t length, const DynLibInfo& lib)
{
	uintptr_t startAddress = reinterpret_cast<uintptr_t>(source);
	if (source == NULL || length == 0 || length - 1 > (std::numeric_limits<uintptr_t>::max)() - startAddress)
	{
		return false;
	}
#if defined (_LINUX)
	Dl_info startInfo;
	Dl_info endInfo;
	const void* endAddress = reinterpret_cast<const void*>(startAddress + length - 1);
	if (dladdr(source, &startInfo) == 0 || dladdr(endAddress, &endInfo) == 0
		|| startInfo.dli_fbase != lib.baseAddress || endInfo.dli_fbase != lib.baseAddress)
	{
		return false;
	}
	iovec local = { destination, length };
	iovec remote = { const_cast<void*>(source), length };
	ssize_t copied = process_vm_readv(getpid(), &local, 1, &remote, 1, 0);
	if (copied == (ssize_t)length)
	{
		return true;
	}
	// process_vm_readv is often blocked for game servers (seccomp / container profiles: EPERM,
	// ENOSYS). Only EFAULT and short reads mean the memory itself is not readable.
	return copied < 0 && errno != EFAULT && ReadMemoryThroughPipe(destination, source, length);
#elif defined (_WIN32)
	if (!IsInModule(source, length, lib))
	{
		return false;
	}
	SIZE_T bytesRead = 0;
	return ReadProcessMemory(GetCurrentProcess(), source, destination, length, &bytesRead)
		&& bytesRead == length;
#endif
}

#endif // __MODULEMEMORY_H__
