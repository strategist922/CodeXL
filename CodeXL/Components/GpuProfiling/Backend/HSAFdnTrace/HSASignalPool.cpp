//==============================================================================
// Copyright (c) 2016 Advanced Micro Devices, Inc. All rights reserved.
/// \author AMD Developer Tools Team
/// \file
/// \brief  This file contains a signal pool class
//==============================================================================

#include "Defs.h"
#include "AutoGenerated/HSATraceInterception.h"
#include "HSASignalPool.h"

HSASignalPool::HSASignalPool()
{
}

HSASignalPool::~HSASignalPool()
{
    Clear();
}

void HSASignalPool::Clear()
{
    size_t poolSize = m_signalPool.size();

    for (size_t i = 0; i < poolSize; i++)
    {
        g_pRealCoreFunctions->hsa_signal_destroy_fn(m_signalPool.top());
        m_signalPool.pop();
    }

    SpAssert(m_signalPool.empty())
}

bool HSASignalPool::AcquireSignal(hsa_signal_value_t initialValue, hsa_signal_t& signal)
{
    if (0 == m_signalPool.size())
    {
        hsa_status_t status = g_pRealCoreFunctions->hsa_signal_create_fn(initialValue, 0, nullptr, &signal);
        SpAssert(HSA_STATUS_SUCCESS == status);

        return HSA_STATUS_SUCCESS == status;
    }

    {
        AMDTScopeLock lock(m_signalPoolMtx);

        if (0 == m_signalPool.size())
        {
            hsa_status_t status = g_pRealCoreFunctions->hsa_signal_create_fn(initialValue, 0, nullptr, &signal);
            SpAssert(HSA_STATUS_SUCCESS == status);

            return HSA_STATUS_SUCCESS == status;
        }

        signal = m_signalPool.top();
        m_signalPool.pop();
        g_pRealCoreFunctions->hsa_signal_store_relaxed_fn(signal, initialValue);

        return true;
    }
}

bool HSASignalPool::ReleaseSignal(hsa_signal_t signal)
{
    if (s_MAX_POOL_SIZE < m_signalPool.size())
    {
        g_pRealCoreFunctions->hsa_signal_destroy_fn(signal);
    }
    else
    {
        AMDTScopeLock lock(m_signalPoolMtx);
        m_signalPool.push(signal);
    }

    return true;
}
