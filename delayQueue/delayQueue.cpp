/**
 * This file implements a fixed delay queue that interatively reads a line from
 * standard input, delays it by a fixed amount, and then writes the line to
 * standard output.
 *
 * Copyright 2018, University Corporation for Atmospheric Research
 * All rights reserved. See file COPYRIGHT in the top-level source-directory for
 * copying and redistribution conditions.
 * 
 *        File: delayQueue.cpp
 *  Created on: Jul 2, 2018
 *      Author: Steven R. Emmerson
 */
#include "config.h"

#include "FixedDelayQueue.h"
#include "log.h"

#include <future>
#include <getopt.h>
#include <iostream>
#include <string>

typedef std::chrono::duration<double, std::ratio<1>> Duration;
typedef FixedDelayQueue<std::string, Duration>       DelayQ;

static const std::string END_STRING{"This is the end string"};

//template class FixedDelayQueue<std::string, std::chrono::microseconds>;

static void decodeCommand(
        int          argc,
        char**       argv,
        double&      seconds)
{
    argc -= optind;
    argv += optind;

    if (argc < 1)
        throw std::runtime_error("Too few arguments");

    if (::sscanf(*argv, "%lg", &seconds) != 1)
        throw std::runtime_error(std::string{"Couldn't decode seconds "
                "specification \""} + *argv + "\"");

    argc -= optind;
    argv += optind;

    if (argc > 0)
        throw std::runtime_error("Too many arguments");
}

static void writeStrings(DelayQ* const delayQ)
{
    for (;;) {
        std::string line{delayQ->pop()};

        if (line.compare(END_STRING) == 0)
            break;

        std::cout << line << '\n';
    }
}

static void stopWriter(DelayQ& delayQ)
{
    delayQ.push(END_STRING);
}

int main(
        const int    argc,
        char** const argv)
{
    int status = 0;

    log_init(argv[0]);

    try {
        double seconds;

        decodeCommand(argc, argv, seconds);

        DelayQ            delayQ{Duration{seconds}};
        std::future<void> fut = std::async(std::launch::async, writeStrings,
                &delayQ);

        try {
            for (;;) {
                std::string line;

                std::getline(std::cin, line);

                if (std::cin.eof())
                    break;
                if (std::cin.bad())
                    throw std::system_error(errno, std::system_category(),
                            "Error reading from standard input");

                delayQ.push(line);
            }
        }
        catch (const std::exception& ex) {
            stopWriter(delayQ);
            fut.get();
            throw;
        }

        stopWriter(delayQ);
        fut.get();
    }
    catch (const std::exception& ex) {
        log_add(ex.what());
        log_flush_error();
        status = 1;
    }

    return status;
}
