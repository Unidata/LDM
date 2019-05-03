/**
 * Copyright (C) 2014 University of Virginia. All rights reserved.
 *
 * @file      testSendApp.cpp
 * @author    Shawn Chen <sc7cq@virginia.edu>
 * @version   1.0
 * @date      Feb 15, 2015
 *
 * @section   LICENSE
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * @brief     A testing application to use the sender side protocol.
 *
 * Since the LDM could be too heavy to use for testing purposes only. This
 * testSendApp is written as a replacement. It can create an instance of the
 * fmtpSendv3 instance and mock the necessary components to get it functioning.
 */


#include "fmtpSendv3.h"

#include <pthread.h>
#include <string.h>
#include <unistd.h>

#include <atomic>
#include <condition_variable>
#include <chrono>
#include <fstream>
#include <iostream>
#include <mutex>
#include <string>
#include <sstream>
#include <thread>
#include <unordered_map>
#include <vector>


std::atomic<uint32_t> notified_prod{0xFFFFFFFF};
std::atomic<uint32_t> expired_prod{0xFFFFFFFF};
std::atomic<uint32_t> curr_prod{0xFFFFFFFF};
std::condition_variable sup;
std::mutex supmtx;
std::unordered_map<uint32_t, char*> pqmap;


/**
 * Allocates a memory region initialized to 0.
 *
 * @param[in] size    Size to dynamically generate.
 */
char* contentGen(unsigned int size)
{
    char* data = new char[size]();
    return data;
}


/**
 * Delete dynamically allocated heap pointer.
 *
 * @param[in] *data    Pointer to the heap.
 */
void contentDestroy(char* data)
{
    delete[] data;
    data = NULL;
}


/**
 * Suppresses silence in feedtypes in order to speed up the replay.
 *
 * @param[in] *ptr    A pointer to a fmtpSendv3 object.
 */
void SilenceSuppressor(void* ptr)
{
    fmtpSendv3 *send = static_cast<fmtpSendv3*>(ptr);
    /**
     * Blocks at getNotify(). Only unblocks when a new product is released.
     * Goes through the set to see if there are still outstanding products
     * with an index less than the one returned by getNotify(). If so, try
     * to suppress silence by notifying the sending thread.
     */
    while (1) {
        notified_prod = send->getNotify();

        #ifdef MODBASE
            uint32_t tmpidx = notified_prod % MODBASE;
        #else
            uint32_t tmpidx = notified_prod;
        #endif

        std::cout << "Earliest product = " << tmpidx << std::endl;
        sup.notify_one();
    }
}


/**
 * Frees the acknowledged memory.
 *
 * @param[in] *ptr    A pointer to a fmtpSendv3 object.
 */
void pqMgr(void* ptr)
{
    fmtpSendv3* send = static_cast<fmtpSendv3*>(ptr);
    while (1) {
        expired_prod = send->releaseMem();
        if (pqmap.count(expired_prod)) {
            char* p = pqmap[expired_prod];
            if (p) {
                contentDestroy(p);
            }
            else {
                std::cout << "Null pointer retrieved." << std::endl;
            }
            pqmap.erase(expired_prod);
        }
        else {
            std::cout << "No valid product found in pqmap." << std::endl;
        }
    }
}


/**
 * Parses a string with given delimitation.
 *
 * @param[in] s            Input string of a line in the metadata.
 * @param[in] delim        Separator.
 * @param[in] elems        Empty vector to contain splitted fields.
 *
 * @return    elems        Vector containing splitted elements.
 */
std::vector<std::string> &split(const std::string &s,
                                char delim,
                                std::vector<std::string> &elems)
{
    std::stringstream ss(s);
    std::string item;
    while (std::getline(ss, item, delim)) {
        elems.push_back(item);
    }
    return elems;
}


/**
 * Parses a string with given delimitation.
 *
 * @param[in] s            Input string of a line in the metadata.
 * @param[in] delim        Separator.
 *
 * @return    elems        Vector containing splitted elements.
 */
std::vector<std::string> split(const std::string &s, char delim)
{
    std::vector<std::string> elems;
    split(s, delim, elems);
    return elems;
}


/**
 * Read a metadata file which contains the file sizes and inteer-arrival time
 * of a metadata log file.

 * @param[in] *sizevec     Vector that contains the size of each product.
 * @param[in] *timevec     Vector that contains the inter-arrival time of
                           each product.
 * @param[in] prodnum      Number of products.
 * @param[in] filename     Filename of the metadata file.
 */
void metaParse(unsigned int* sizevec, unsigned int* timevec,
               unsigned int  prodnum, std::string&  filename)
{
    std::string line;
    std::ifstream fp(filename, std::ios::binary);
    if (fp.is_open()) {
        for(int i=0; i < prodnum; ++i) {
            std::getline(fp, line);

            if (line.find_first_not_of("\t\n ") != std::string::npos) {
                std::vector<std::string> linevec = split(line, ',');
                sizevec[i] = std::stoi(linevec[0]);
                timevec[i] = std::stoi(linevec[2]);
            }
            else {
                std::cout << "newline" << std::endl;
            }
        }
    }
    fp.close();
}


/**
 * Since the LDM could be too heavy to use for testing purposes only. This main
 * function is a light weight replacement of the LDM sending application. It
 * sets up the whole environment and call sendProduct() to send data. All the
 * arguments are passed in through command line.
 *
 * @param[in] tcpAddr      IP address of the sender.
 * @param[in] tcpPort      Port number of the sender.
 * @param[in] mcastAddr    multicast address of the group.
 * @param[in] mcastPort    Port number of the multicast group.
 * @param[in] ifAddr       IP of the interface to set as default.
 * @param[in] filename     filename of the metadata.
 */
int main(int argc, char const* argv[])
{
    if (argc < 4) {
        std::cerr << "ERROR: Insufficient arguments." << std::endl;
        return 1;
    }

    std::string tcpAddr(argv[1]);
    const unsigned short tcpPort = (unsigned short)atoi(argv[2]);
    std::string mcastAddr(argv[3]);
    const unsigned short mcastPort = (unsigned short)atoi(argv[4]);
    std::string ifAddr(argv[5]);
    std::string filename(argv[6]);

    char tmp[] = "test metadata";
    char* metadata = tmp;
    unsigned int metaSize = sizeof(tmp);

    fmtpSendv3* sender =
        new fmtpSendv3(tcpAddr.c_str(), tcpPort, mcastAddr.c_str(), mcastPort,
                        0, 1, ifAddr.c_str());

    sender->Start();
    sleep(180);

    /**
     * specify how many data products to send, this is the amount of lines
     * to read in the metadata file.
     */
    unsigned int prodnum = PRODNUM;
    /* array to store size of each product */
    unsigned int * sizevec = new unsigned int[prodnum];
    /* array to store inter-arrival time of each product */
    unsigned int * timevec = new unsigned int[prodnum];
    metaParse(sizevec, timevec, prodnum, filename);

    std::thread t(SilenceSuppressor, sender);
    std::thread pqm(pqMgr, sender);
    t.detach();
    pqm.detach();

    for(int run=0; run < EXPTRUN; ++run) {
        for(int i=0; i < prodnum; ++i) {
            /* generate pareto distributed data */
            char * data = contentGen(sizevec[i]);
            curr_prod = sender->sendProduct(data, sizevec[i], metadata,
                                            metaSize);
            /* put dynamic product pointer into a mocked pq */
            pqmap[curr_prod] = data;

            /**
             * Condition variable blocks the thread, either sleeping for the
             * inter-arrival time or waiting to be notified. A lambda expression
             * here guarantees that silence can only be suppressed if there is
             * no active products to be served.
             */
            {
                std::unique_lock<std::mutex> sup_lk(supmtx);
                sup.wait_for(sup_lk,
                        std::chrono::microseconds(timevec[i] * 1000),
                        []() {return (curr_prod < notified_prod);} );
            }
        }
        sleep(60);
        /* clear the prodset for current run */
        sender->clearRuninProdSet(run + 1);
    }

    std::cout << EXPTRUN << " runs finished" << std::endl;
    while(1);

    delete[] sizevec;
    delete[] timevec;
    delete sender;
    return 0;
}
