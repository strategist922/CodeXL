//==============================================================================
// Copyright (c) 2016 Advanced Micro Devices, Inc. All rights reserved.
/// \author AMD Developer Tools Team
/// \file
/// \brief  This file contains code to collect timestamps from AQL packets
//==============================================================================

#include "../Common/Defs.h"
#include "AutoGenerated/HSATraceInterception.h"
#include "HSASignalPool.h"
#include "HSAAqlPacketTimeCollector.h"

HSATimeCollectorGlobals::HSATimeCollectorGlobals() :
    m_doQuit(false)
{
#if defined (_LINUX) || defined (LINUX)
    if (!m_dispatchesInFlight.lockCondition())
    {
        Log(logERROR, "unable to lock condition\n");
    }
#endif
}

void HSASignalQueue::AddSignalToBack(const HSAPacketSignalReplacer& signal)
{
    AMDTScopeLock lock(m_signalQueueMtx);
    m_signalQueue.push(signal);
}

void HSASignalQueue::GetSignalFromFront(HSAPacketSignalReplacer& outSignal)
{
    AMDTScopeLock lock(m_signalQueueMtx);
    outSignal = m_signalQueue.front();
    m_signalQueue.pop();
}

size_t HSASignalQueue::GetSize() const
{
    return m_signalQueue.size();
}

void HSASignalQueue::Clear()
{
    while (!m_signalQueue.empty())
    {
        m_signalQueue.pop();
    }
}

const unsigned int HSASignalCollectorThread::m_deferLen;

HSASignalCollectorThread::HSASignalCollectorThread() : osThread(gtString(L"HSASignalCollectorThread"), false, false /*true*/),
    m_index(0)
{
    hsa_signal_t& forceSignalCollection = HSATimeCollectorGlobals::Instance()->m_forceSignalCollection;
    hsa_status_t status = g_pRealCoreFunctions->hsa_signal_create_fn(0, 0, nullptr, &forceSignalCollection);

    if (HSA_STATUS_SUCCESS != status)
    {
        Log(logERROR, "Unable to create signal\n");
    }
}

int HSASignalCollectorThread::entryPoint()
{
    int retVal = 0;

    hsa_signal_t signalList[2];
    hsa_signal_value_t signalValueList[2];
    hsa_signal_condition_t signalConditionList[2];

    signalList[0] = HSATimeCollectorGlobals::Instance()->m_forceSignalCollection;
    signalValueList[0] = 1;
    signalConditionList[0] = HSA_SIGNAL_CONDITION_EQ;

    signalValueList[1] = 1;
    signalConditionList[1] = HSA_SIGNAL_CONDITION_LT;

    bool doFlush = false;
    bool doIdleFlush = false;

    // provide local aliases to some of the globals
    bool& doQuit = HSATimeCollectorGlobals::Instance()->m_doQuit;
    static unsigned int numDispatches = 0;

    while (true)
    {
        while (0 != HSASignalQueue::Instance()->GetSize() || doFlush)
        {
            if (!doFlush)
            {
                HSASignalQueue::Instance()->GetSignalFromFront(m_deferList[m_index]);
                signalList[1] = m_deferList[m_index].m_profilerSignal;
            }

            doFlush = false;

            if (1 == g_pRealAmdExtFunctions->hsa_amd_signal_wait_any_fn(2, signalList, signalConditionList, signalValueList, static_cast<uint64_t>(-1), HSA_WAIT_STATE_BLOCKED, nullptr))
            {
                if (0 != m_deferList[m_index].m_originalSignal.handle)
                {
                    // update the original signal so the app will know that it is complete
                    g_pRealCoreFunctions->hsa_signal_add_relaxed_fn(m_deferList[m_index].m_originalSignal, -1);
                }

                m_index++;
            }
            else
            {
                doFlush = true;
            }

            if (doQuit)
            {
                doFlush = true;
            }

            if (m_index == m_deferLen || doFlush)
            {
                for (unsigned int i = 0; i < m_index; i++)
                {
                    HSAPacketSignalReplacer& replacer = m_deferList[i];

                    hsa_amd_profiling_dispatch_time_t time;
                    g_pRealAmdExtFunctions->hsa_amd_profiling_get_dispatch_time_fn(replacer.m_agent, replacer.m_profilerSignal, &time);
                    numDispatches++;
                    replacer.m_pAqlPacket->SetTimestamps(time.start, time.end);

                    HSASignalPool::Instance()->ReleaseSignal(replacer.m_profilerSignal);
                }

                if (doFlush)
                {
#ifdef FUTURE_ROCR_VERSION
                    g_pRealCoreFunctions->hsa_signal_store_screlease_fn(signalList[0], 0); // this is HSATimeCollectorGlobals::Instance()->m_forceSignalCollection
#else
                    g_pRealCoreFunctions->hsa_signal_store_release_fn(signalList[0], 0); // this is HSATimeCollectorGlobals::Instance()->m_forceSignalCollection
#endif
                    m_deferList[0] = m_deferList[m_index];
                }

                m_index = 0;
            }

            if (doQuit)
            {
                return retVal;
            }

            if (doIdleFlush)
            {
                doFlush = false;
                doIdleFlush = false;
            }
        }

#if defined (_LINUX) || defined (LINUX)
        HSATimeCollectorGlobals::Instance()->m_dispatchesInFlight.waitForCondition();
#endif

        if (1 == g_pRealCoreFunctions->hsa_signal_load_relaxed_fn(signalList[0]))
        {
            doFlush = true;
            doIdleFlush = true;
            signalList[1] = signalList[0];
        }
    }

    return retVal;
}
