/*
 * Copyright (c) 2013 Ambroz Bizjak
 * All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef AMBROLIB_AD5206_CURRENT_H
#define AMBROLIB_AD5206_CURRENT_H

#include <stdint.h>

#include <aprinter/meta/Object.h>
#include <aprinter/meta/BitsInInt.h>
#include <aprinter/meta/WrapFunction.h>
#include <aprinter/meta/TypeListGet.h>
#include <aprinter/meta/FixedPoint.h>
#include <aprinter/base/DebugObject.h>
#include <aprinter/base/Assert.h>

#include <aprinter/BeginNamespace.h>

template <
    typename TSsPin,
    template <typename, typename, typename, int> class TSpiTemplate
>
struct Ad5206CurrentParams {
    using SsPin = TSsPin;
    template <typename X, typename Y, typename Z, int W> using SpiTemplate = TSpiTemplate<X, Y, Z, W>;
};

template <
    uint8_t TDevChannelIndex,
    typename TConversionFactor
>
struct Ad5206CurrentChannelParams {
    static uint8_t const DevChannelIndex = TDevChannelIndex;
    using ConversionFactor = TConversionFactor;
};

template <typename Context, typename ParentObject, typename Params, typename ChannelsList>
class Ad5206Current {
public:
    struct Object;
    
private:
    struct SpiHandler;
    
    static int const NumDevChannels = 6;
    static int const SpiMaxCommands = 2;
    static int const SpiCommandBits = BitsInInt<SpiMaxCommands>::value;
    using TheSpi = typename Params::template SpiTemplate<Context, Object, SpiHandler, SpiCommandBits>;
    
public:
    static void init (Context c)
    {
        auto *o = Object::self(c);
        
        Context::Pins::template set<typename Params::SsPin>(c, true);
        Context::Pins::template setOutput<typename Params::SsPin>(c);
        TheSpi::init(c);
        o->m_current_channel = 0xFF;
        o->m_delaying = false;
        for (uint8_t i = 0; i < NumDevChannels; i++) {
            o->m_pending[i] = false;
        }
        
        o->debugInit(c);
    }
    
    static void deinit (Context c)
    {
        auto *o = Object::self(c);
        o->debugDeinit(c);
        
        TheSpi::deinit();
        Context::Pins::template set<typename Params::SsPin>(c, true);
    }
    
    template <int ChannelIndex>
    static void setCurrent (Context c, float current_ma)
    {
        auto *o = Object::self(c);
        o->debugAccess(c);
        
        using ChannelParams = TypeListGet<ChannelsList, ChannelIndex>;
        uint8_t const dev_channel = ChannelParams::DevChannelIndex;
        static_assert(dev_channel < 6, "");
        o->m_data[dev_channel] = FixedPoint<8, false, 0>::importFpSaturatedRound(current_ma * (float)ChannelParams::ConversionFactor::value()).bitsValue();
        o->m_pending[dev_channel] = true;
        if (o->m_current_channel == 0xFF) {
            send_command(c, dev_channel);
        }
    }
    
    using GetSpi = TheSpi;
    
    using EventLoopFastEvents = typename TheSpi::EventLoopFastEvents;
    
private:
    static void spi_handler (Context c)
    {
        auto *o = Object::self(c);
        o->debugAccess(c);
        AMBRO_ASSERT(o->m_current_channel != 0xFF)
        AMBRO_ASSERT(TheSpi::endReached(c))
        
        if (!o->m_delaying) {
            o->m_delaying = true;
            Context::Pins::template set<typename Params::SsPin>(c, true);
            TheSpi::cmdWriteByte(c, 0xFF, 15);
            return;
        }
        o->m_delaying = false;
        for (uint8_t i = 1; i <= NumDevChannels; i++) {
            uint8_t dev_channel = (i >= NumDevChannels - o->m_current_channel) ? (i - (NumDevChannels - o->m_current_channel)) : (o->m_current_channel + i);
            if (o->m_pending[dev_channel]) {
                send_command(c, dev_channel);
                return;
            }
        }
        o->m_current_channel = 0xFF;
    }
    
    static void send_command (Context c, uint8_t dev_channel)
    {
        auto *o = Object::self(c);
        AMBRO_ASSERT(o->m_pending[dev_channel])
        AMBRO_ASSERT(!o->m_delaying)
        
        o->m_pending[dev_channel] = false;
        o->m_current_channel = dev_channel;
        o->m_current_data = o->m_data[dev_channel];
        Context::Pins::template set<typename Params::SsPin>(c, false);
        TheSpi::cmdWriteBuffer(c, dev_channel, &o->m_current_data, 1);
    }
    
    struct SpiHandler : public AMBRO_WFUNC_TD(&Ad5206Current::spi_handler) {};
    
public:
    struct Object : public ObjBase<Ad5206Current, ParentObject, MakeTypeList<
        TheSpi
    >>,
        public DebugObject<Context, void>
    {
        TheSpi m_spi;
        uint8_t m_current_channel;
        bool m_delaying;
        uint8_t m_data[NumDevChannels];
        bool m_pending[NumDevChannels];
        uint8_t m_current_data;
    };
};

#include <aprinter/EndNamespace.h>

#endif
