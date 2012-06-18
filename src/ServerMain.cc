/* Copyright (c) 2009-2012 Stanford University
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR(S) DISCLAIM ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL AUTHORS BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/**
 * \file
 * This file provides the main program for RAMCloud storage servers.
 */

#include "Context.h"
#if INFINIBAND
#include "InfRcTransport.h"
#endif
#include "OptionParser.h"
#include "Server.h"
#include "ShortMacros.h"
#include "TransportManager.h"

int
main(int argc, char *argv[])
{
    using namespace RAMCloud;
    Context context(true);
    try {
        ServerConfig config = ServerConfig::forExecution();
        string masterTotalMemory, hashTableMemory;

        bool masterOnly;
        bool backupOnly;

        OptionsDescription serverOptions("Server");
        serverOptions.add_options()
            ("backupInMemory,m",
             ProgramOptions::bool_switch(&config.backup.inMemory),
             "Backup will store segment replicas in memory")
            ("backupOnly,B",
             ProgramOptions::bool_switch(&backupOnly),
             "The server should run the backup service only (no master)")
            ("backupStrategy",
             ProgramOptions::value<int>(&config.backup.strategy)->
               default_value(RANDOM_REFINE_AVG),
             "0 random refine min, 1 random refine avg, 2 even distribution, "
             "3 uniform random")
            ("disableLogCleaner,d",
             ProgramOptions::bool_switch(&config.master.disableLogCleaner),
             "Disable the log cleaner entirely. You will eventually run out "
             "of memory, but at least you can do so faster this way.")
            ("file,f",
             ProgramOptions::value<string>(&config.backup.file)->
                default_value("/var/tmp/backup.log"),
             "The file path to the backup storage.")
            ("hashTableMemory,h",
             ProgramOptions::value<string>(&hashTableMemory)->
                default_value("10%"),
             "Percentage or megabytes of master memory allocated to "
             "the hash table")
            ("masterOnly,M",
             ProgramOptions::bool_switch(&masterOnly),
             "The server should run the master service only (no backup)")
            ("totalMasterMemory,t",
             ProgramOptions::value<string>(&masterTotalMemory)->
                default_value("10%"),
             "Percentage or megabytes of system memory for master log & "
             "hash table")
            ("replicas,r",
             ProgramOptions::value<uint32_t>(&config.master.numReplicas)->
                default_value(0),
             "Number of backup copies to make for each segment")
            ("segmentFrames",
             ProgramOptions::value<uint32_t>(&config.backup.numSegmentFrames)->
                default_value(512),
             "Number of segment frames in backup storage")
            ("detectFailures",
             ProgramOptions::value<bool>(&config.detectFailures)->
                default_value(true),
             "Whether to use the randomized failure detector")
            ("clusterName",
             ProgramOptions::value<string>(&config.clusterName),
             "Controls the reuse of replicas stored on this backup."
             "'Tags' replicas created on the backup with this cluster name. "
             "This has two effects. First, any replicas found in storage "
             "are discarded unless they are tagged with an identical cluster "
             "name. Second, any replicas created by the backup process will "
             "only be reused by future backup processes if the cluster name "
             "on the stored replica matches the cluster name of future"
             "process. The name '__unnamed__' is special and never matches "
             "any cluster name (even itself), so it guarantees all stored "
             "replicas are discarded on start and that all replicas created "
             "by this process are discarded by future backups. "
             "This is convenient for testing.");

        OptionParser optionParser(serverOptions, argc, argv);

        // Log all the command-line arguments.
        string args;
        for (int i = 0; i < argc; i++) {
            if (i != 0)
                args.append(" ");
            args.append(argv[i]);
        }
        LOG(NOTICE, "Command line: %s", args.c_str());

        if (masterOnly && backupOnly)
            DIE("Can't specify both -B and -M options");

        if (masterOnly) {
            config.services = {MASTER_SERVICE,
                               MEMBERSHIP_SERVICE, PING_SERVICE};
        } else if (backupOnly) {
            config.services = {BACKUP_SERVICE,
                               MEMBERSHIP_SERVICE, PING_SERVICE};
        } else {
            config.services = {MASTER_SERVICE, BACKUP_SERVICE,
                               MEMBERSHIP_SERVICE, PING_SERVICE};
        }

        const string localLocator = optionParser.options.getLocalLocator();

#if INFINIBAND
        InfRcTransport<>::setName(localLocator.c_str());
#endif
        context.transportManager->setTimeout(
                optionParser.options.getTransportTimeout());
        context.transportManager->initialize(localLocator.c_str());

        config.coordinatorLocator =
            optionParser.options.getCoordinatorLocator();
        // Transports may augment the local locator somewhat.
        // Make sure the server is aware of that augmented locator.
        config.localLocator =
            context.transportManager->getListeningLocatorsString();

        LOG(NOTICE, "%s: Listening on %s",
            config.services.toString().c_str(), config.localLocator.c_str());

        if (!backupOnly) {
            LOG(NOTICE, "Using %u backups", config.master.numReplicas);
            config.setLogAndHashTableSize(masterTotalMemory, hashTableMemory);
        }

        Server server(context, config);
        server.run(); // Never returns except for exceptions.

        return 0;
    } catch (const Exception& e) {
        LOG(ERROR, "Fatal error in server at %s: %s",
            context.transportManager->getListeningLocatorsString().c_str(),
            e.what());
        return 1;
    } catch (const std::exception& e) {
        LOG(ERROR, "Fatal error in server at %s: %s",
            context.transportManager->getListeningLocatorsString().c_str(),
            e.what());
        return 1;
    } catch (...) {
        LOG(ERROR, "Unknown fatal error in server at %s",
            context.transportManager->getListeningLocatorsString().c_str());
        return 1;
    }
}
