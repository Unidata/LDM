/**
 * This file implements a manager of pools of IPv4 addresses. Each pool can be
 * shared by multiple processes and accessed concurrently. Initial population of
 * any pool (e.g., by UpMcastMgr) causes interprocess communication resources to
 * be created. These resources are released when the process that created them
 * terminates.
 *
 * Copyright 2018 University Corporation for Atmospheric Research. All rights
 * reserved. See the file COPYRIGHT in the top-level source-directory for
 * licensing conditions.
 *
 *        File: InAddrMgr.cpp
 *  Created on: Jan 9, 2018
 *      Author: Steven R. Emmerson
 */
#include "config.h"

#include "log.h"
#include "InAddrMgr.h"
#include "ldmprint.h"

#include <cassert>
#include <cerrno>
#include <climits>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <memory>
#include <mutex>
#include <semaphore.h>
#include <stdlib.h>
#include <string>
#include <sys/fcntl.h>
#include <sys/stat.h>
#include <stdexcept>
#include <sys/mman.h>
#include <system_error>
#include <unistd.h>
#include <unordered_map>
#include <vector>

class ShmAlloc
{
    typedef unsigned long EltType;

    class Impl final
    {
        std::string       name;
        const std::size_t nbytes;
        const ::pid_t     pid;
        EltType*          ptr;

        /**
         * Deletes any previously-existing shared-memory segment.
         * @return File descriptor of shared-memory segment
         * @throw std::system_error  Couldn't create shared-memory
         * @throw std::system_error  Couldn't set size of shared-memory
         */
        int create()
        {
            ::shm_unlink(name.c_str()); // Creating so failure is OK
            auto fd = ::shm_open(name.c_str(), O_RDWR|O_CREAT|O_EXCL,
                    S_IRUSR|S_IWUSR);
            if (fd < 0)
                throw std::system_error(errno, std::system_category(),
                        std::string{"Couldn't create shared-memory "} + name);
            if (::ftruncate(fd, nbytes)) {
                ::shm_unlink(name.c_str());
                throw std::system_error(errno, std::system_category(),
                        std::string{"Couldn't set size of shared-memory " +
                        name + " to " + std::to_string(nbytes) + " bytes"});
            }
            return fd;
        }

        void memory_map(const int fd)
        {
            ptr = static_cast<EltType*>(::mmap(nullptr, nbytes,
                    PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0));
            if (ptr == MAP_FAILED)
                throw std::system_error(errno, std::system_category(),
                        std::string{"Couldn't memory-map shared-memory " +
                                name});
        }

    public:
        typedef bool value_type;

        /**
         * Constructs. Deletes any previously-existing shared-memory segment
         * with the same name.
         * @param[in] prefix         Prefix for shared-memory segment's name
         * @param[in] nbytes         Size of shared-memory segment
         * @throw std::system_error  Couldn't open shared-memory segment
         * @throw std::system_error  Couldn't set size of shared-memory segment
         * @throw std::system_error  Couldn't memory-map shared-memory segment
         */
        Impl(   const std::string& prefix,
                const std::size_t  nbytes)
            : name{prefix + "_shm"}
            , nbytes{nbytes}
            , pid{::getpid()}
            , ptr{nullptr}
        {
            auto fd = create();
            try {
                memory_map(fd);
                ::close(fd);
            }
            catch (const std::exception& ex) {
                ::close(fd);
                ::shm_unlink(name.c_str());
                throw;
            }
        }

        /**
         * Destroys.
         */
        ~Impl() noexcept
        {
            if (::munmap(ptr, nbytes))
                log_warning("Couldn't un-memory-map shared-memory %s: %s",
                        name.c_str(), ::strerror(errno));
            if (pid == ::getpid() && ::shm_unlink(name.c_str()))
                log_warning("Couldn't unlink shared-memory %s: %s",
                        name.c_str(), ::strerror(errno));
        }

        /**
         * Allocates memory. Does nothing but return the pointer to the shared-
         * memory segment.
         * @param[in] n   Amount of memory to allocate. Ignored.
         * @return        Pointer to allocated memory
         * @threadsafety  Safe
         */
        EltType* allocate(std::size_t n) const noexcept
        {
            assert(n <= nbytes * CHAR_BIT);
            return ptr;
        }

        /**
         * Deallocates memory. Does nothing.
         * @param[in] p   Pointer to allocated memory. Ignored.
         * @param[in] n   Number of bytes to deallocate. Ignored.
         * @threadsafety  Safe
         */
        void deallocate(EltType* p, std::size_t n) const noexcept
        {}
    };

    /**
     * A `std::shared_ptr` is used so that the shared-memory segment is unmapped
     * only when the last reference to it is destroyed. Recall that
     * `std::vector<bool>` makes a copy of the allocator.
     */
    std::shared_ptr<Impl> pImpl;

public:
    typedef bool value_type;

    /**
     * Default constructs.
     */
    ShmAlloc() noexcept
        : pImpl{}
    {}

    /**
     * Constructs. Deletes any previously-existing shared-memory segment with
     * the same name.
     * @param[in] prefix         Prefix for shared-memory segment's name
     * @param[in] nbytes         Size of shared-memory segment
     * @throw std::system_error  Couldn't create shared-memory segment
     * @throw std::system_error  Couldn't set size of shared-memory segment
     * @throw std::system_error  Couldn't memory-map shared-memory segment
     */
    ShmAlloc(
            const std::string& prefix,
            const std::size_t  nbytes)
        : pImpl{new Impl(prefix, nbytes)}
    {}

    /**
     * Allocates memory.
     * @param[in] n   Number of elements to allocate. Ignored.
     * @return        Pointer to shared-memory segment
     * @threadsafety  Safe
     */
    EltType* allocate(std::size_t n) const noexcept
    {
        return pImpl->allocate(n);
    }

    /**
     * Deallocates memory.
     * @param[in] p   Pointer to memory to deallocate. Ignored.
     * @param[in] n   Number of elements to deallocate. Ignored.
     * @threadsafety  Safe
     */
    void deallocate(EltType* p, std::size_t n) const noexcept
    {
        pImpl->deallocate(p, n);
    }

    template<class U> struct rebind { typedef ShmAlloc other; };
}; // class ShmAlloc

/******************************************************************************/

class InAddrPool final
{
    class Semaphore final
    {
        typedef std::shared_ptr<::sem_t> SemPtr;

        const std::string name;
        const ::pid_t     pid;
        SemPtr            semPtr;

    public:
        /**
         * Constructs. Deletes any previously-existing semaphore with the same
         * name. The semaphore is locked upon return.
         * @param[in] prefix         Prefix for semaphore's name
         * @throw std::system_error  Semaphore couldn't be created
         */
        Semaphore(const std::string& prefix)
            : name{prefix + "_sem"}
            , pid{::getpid()}
            , semPtr{nullptr}
        {
            ::sem_unlink(name.c_str()); // Being created so failure is OK
            // No-op deleter because semaphore not allocated by `new()`
            semPtr = SemPtr{::sem_open(name.c_str(), O_CREAT|O_EXCL,
                    S_IRUSR|S_IWUSR, 0), [](::sem_t* p){}};
            if (semPtr.get() == SEM_FAILED)
                throw std::system_error(errno, std::system_category(),
                        std::string("Couldn't create semaphore ") + name);
        }

        ~Semaphore() noexcept
        {
            if (semPtr.unique()) {
                ::sem_close(semPtr.get());
                if (pid == ::getpid()) // Creating process destroys
                    ::sem_unlink(name.c_str());
            }
        }

        /**
         * @throw std::system_error  Couldn't lock semaphore
         */
        void lock()
        {
            if (::sem_wait(semPtr.get()))
                throw std::system_error(errno, std::system_category(),
                        std::string("Couldn't lock semaphore ") + name);
        }

        void unlock() noexcept
        {
            ::sem_post(semPtr.get());
        }
    }; // class Semaphore

    /**
     * RAII class for locking a `Semaphore`.
     */
    class Lock final
    {
        Semaphore& sem;
    public:
        /**
         * @param sem
         * @throw std::system_error  Couldn't lock semaphore
         */
        Lock(Semaphore& sem) : sem(sem) { sem.lock(); }
        ~Lock() noexcept                { sem.unlock(); }
        Lock(const Lock& lock)            =delete;
        Lock(const Lock&& lock)           =delete;
        Lock& operator=(const Lock& rhs)  =delete;
        Lock& operator=(const Lock&& rhs) =delete;
    };

    /// Prefix for IPC names
    const std::string           namePrefix;
    /// Semaphore for concurrent access by multiple processes
    Semaphore                   sem;
    /// Number of addresses
    const std::size_t           numAddrs;
    /// The reserved-state of each address
    std::vector<bool, ShmAlloc> isReserved;
    /// Network prefix in network byte-order
    const in_addr_t             networkPrefix;

    /**
     * Returns the prefix for the name of IPC objects.
     * @param[in] feed            LDM feed
     * @return                    Prefix for the name of IPC objects
     * @throw std::runtime_error  Couldn't get user-name
     */
    static std::string getNamePrefix(const feedtypet feed)
    {
        std::string prefix("/");
        char* userName = ::getenv("LOGNAME");
        if (userName == nullptr)
            userName = ::getenv("USER");
        if (userName == nullptr)
            throw std::runtime_error("Couldn't get user-name");
        prefix += userName;
        prefix += "_InAddrPool_";
        char feedStr[256];
        ft_format(feed, feedStr, sizeof(feedStr));
        prefix += feedStr;
        return prefix;
    }

    /**
     * Returns the number of IPv4 addresses in a subnet.
     * @param[in] prefixLen          Length of network prefix in bits
     * @return                       Number of addresses
     * @throw std::invalid_argument  `prefixLen >= 31`
     * @threadsafety                 Safe
     */
    static std::size_t getNumAddrs(const unsigned prefixLen)
    {
        if (prefixLen >= 31)
            throw std::invalid_argument("Invalid network prefix length: " +
                    std::to_string(prefixLen));
        return 1 << (32 - prefixLen);
    }

public:
    /**
     * Constructs. Overwrites any previously-existing pool for the same feed.
     * @param[in] feed               LDM feed
     * @param[in] networkPrefix      prefix in network byte-order
     * @param[in] prefixLen          Length of network prefix in bits
     * @throw std::runtime_error     Couldn't get user-name
     * @throw std::invalid_argument  `prefixLen >= 31`
     * @throw std::invalid_argument  `networkPrefix` and `prefixLen` are
     *                               incompatible
     * @throw std::system_error      Couldn't create shared-memory segment
     * @throw std::system_error      Couldn't set numAddrs of shared-memory segment
     * @throw std::system_error      Couldn't memory-map shared-memory segment
     */
    InAddrPool(
            const feedtypet feed,
            const in_addr   networkPrefix,
            const unsigned  prefixLen)
        : namePrefix{getNamePrefix(feed)}
        , sem{namePrefix}
        , numAddrs{getNumAddrs(prefixLen)}
        /*
         * For an unknown reason, using the 1-argument constructor,
         * `isReserved{ShmAlloc{...}}`, results in a SIGSEGV when an element is
         * accessed.
         */
        , isReserved{numAddrs, false,
            ShmAlloc{namePrefix, (numAddrs + CHAR_BIT - 1) / CHAR_BIT}}
        , networkPrefix{networkPrefix.s_addr}
    {
        if (ntohl(networkPrefix.s_addr) & ((1ul<<(32-prefixLen))-1)) {
            char dottedQuad[INET_ADDRSTRLEN];
            throw std::invalid_argument(std::string("Network prefix ") +
                    inet_ntop(AF_INET, &networkPrefix.s_addr, dottedQuad,
                            sizeof(dottedQuad)) +
                            " is incompatible with prefix length " +
                            std::to_string(prefixLen));
        }
        sem.unlock();
    }

    /**
     * Reserves an address. The address will be unique.
     * @return                    Reserved address
     * @throw std::system_error   Couldn't lock semaphore
     * @throw std::runtime_error  No address is available
     * @threadsafety              Compatible but not safe
     * @exceptionsafety           Strong guarantee
     */
    struct in_addr reserve()
    {
        Lock lock{sem};
        std::size_t i;
        // Avoid network identifier address and broadcast address
        for (i = 1; i < (numAddrs-1) && isReserved[i]; ++i)
            ;
        if (i >= numAddrs - 1)
            throw std::runtime_error("All IPv4 addresses are in use");
        isReserved[i] = true;
        struct in_addr addr = {networkPrefix | htonl(i)};
        return addr;
    }

    /**
     * Releases an address so that it can be subsequently reserved.
     * @param[in] addr          Reserved address to be released
     * @throw std::system_error Couldn't lock semaphore
     * @throw std::logic_error  `addr` wasn't reserved
     * @threadsafety            Compatible but not safe
     * @exceptionsafety         Strong guarantee
     */
    void release(const struct in_addr addr)
    {
        Lock lock{sem};
        std::size_t i = ntohl(addr.s_addr & ~networkPrefix);
        if (!isReserved.at(i))
            throw std::logic_error("IPv4 address not reserved");
        isReserved[i] = false;
    }
}; // class InAddrPool

/******************************************************************************/

class InAddrMgr final
{
    std::unordered_map<feedtypet, InAddrPool> addrPools;

public:
    /**
     * Adds a pool of addresses for a feed. Overwrites any previously-existing
     * pool for the same feed. The pool will be shared by all child processes
     * and deleted when the current process terminates normally or
     * `inam_clear()` is called.
     * @param[in] feed               LDM feed
     * @param[in] networkPrefix      Network prefix in network byte-order
     * @param[in] prefixLen          Length of network prefix in bits
     * @throw std::runtime_error     Couldn't get user-name
     * @throw std::invalid_argument  `prefixLen >= 31`
     * @throw std::invalid_argument  `networkPrefix` and `prefixLen` are
     *                               incompatible
     * @throw std::system_error      Couldn't create shared-memory segment
     * @throw std::system_error      Couldn't set size of shared-memory segment
     * @throw std::system_error      Couldn't memory-map shared-memory segment
     * @threadsafety                 Compatible but not safe
     * @exceptionsafety              Strong guarantee
     */
    void add(
            const feedtypet feed,
            const in_addr   networkPrefix,
            const unsigned  prefixLen)
    {
        if (addrPools.erase(feed)) {
            char feedStr[256];
            ::ft_format(feed, feedStr, sizeof(feedStr));
            log_notice("Overwriting address-pool for feed %s", feedStr);
        }
        addrPools.insert({feed, InAddrPool{feed, networkPrefix, prefixLen}});
    }

    /**
     * Reserves an address from the pool created by the previous call to
     * `add()`. The address will be unique and not previously reserved. The
     * reservation will be visible to all child processes. .
     * @param[in] feed            LDM feed
     * @return                    Reserved address
     * @throw std::out_of_range   No address pool for `feed`
     * @throw std::runtime_error  No address is available
     * @threadsafety              Compatible but not safe
     * @exceptionsafety           Strong guarantee
     */
    struct in_addr reserve(const feedtypet feed)
    {
        return addrPools.at(feed).reserve();
    }

    /**
     * Releases an address in the pool created by the previous call to `add()`
     * so that it can be subsequently re-used. The release will be visible to
     * all child processes.
     * @param[in] feed           LDM feed
     * @param[in] addr           Reserved address
     * @throw std::out_of_range  No address pool for `feed`
     * @throw std::logic_error   `addr` wasn't reserved
     * @threadsafety             Compatible but not safe
     * @exceptionsafety          Strong guarantee
     */
    void release(
            const feedtypet       feed,
            const struct in_addr& addr)
    {
        addrPools.at(feed).release(addr);
    }

    void clear() noexcept
    {
        addrPools.clear();
    }
}; // class InAddrMgr

/******************************************************************************/

static void registerInamClear()
{
    if (::atexit(&inam_clear))
        throw std::system_error(errno, std::system_category(),
                "Couldn't register inam_clear()");
}

static InAddrMgr& getMgr()
{
    static InAddrMgr      mgr{};
    static std::once_flag flag{};
    std::call_once(flag, &registerInamClear);
    return mgr;
}

/******************************************************************************/

int inam_add(
        const feedtypet feed,
        const in_addr   networkPrefix,
        const unsigned  prefixLen)
{
    try {
        getMgr().add(feed, networkPrefix, prefixLen);
    }
    catch (const std::invalid_argument& ex) {
        log_add(ex.what());
        return EINVAL;
    }
    catch (const std::runtime_error& ex) {
        log_add(ex.what());
        return ENOENT;
    }
    catch (const std::exception& ex) {
        log_add(ex.what());
        return ENOMEM;
    }
    return 0;
}

Ldm7Status inam_reserve(
        const feedtypet feed,
        struct in_addr* addr)
{
    try {
        *addr = getMgr().reserve(feed);
    }
    catch (const std::out_of_range& ex) {
        log_add(ex.what());
        return LDM7_NOENT;
    }
    catch (const std::exception& ex) {
        log_add(ex.what());
        return LDM7_MCAST;
    }
    return LDM7_OK;
}

Ldm7Status inam_release(
        const feedtypet       feed,
        const struct in_addr* addr)
{
    try {
        getMgr().release(feed, *addr);
    }
    catch (const std::exception& ex) {
        log_add(ex.what());
        return LDM7_NOENT;
    }
    return 0;
}

void inam_clear()
{
    getMgr().clear();
}
