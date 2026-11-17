#pragma once

#include "meter_frame.h"

#include "public.sdk/source/vst/utility/dataexchange.h"
#include "public.sdk/source/vst/vsteditcontroller.h"

namespace fwyc {

class Controller : public Steinberg::Vst::EditController, public Steinberg::Vst::IDataExchangeReceiver
{
public:
    static Steinberg::FUnknown* createInstance (void*)
    {
        return static_cast<Steinberg::Vst::IEditController*> (new Controller ());
    }

    Steinberg::tresult PLUGIN_API initialize (Steinberg::FUnknown* context) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API setComponentState (Steinberg::IBStream* state) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API notify (Steinberg::Vst::IMessage* message) SMTG_OVERRIDE;
    Steinberg::IPlugView* PLUGIN_API createView (Steinberg::FIDString name) SMTG_OVERRIDE;

    // IDataExchangeReceiver: meter frames from the processor, delivered on the main thread.
    void PLUGIN_API queueOpened (Steinberg::Vst::DataExchangeUserContextID userContextID, Steinberg::uint32 blockSize,
                                 Steinberg::TBool& dispatchOnBackgroundThread) SMTG_OVERRIDE;
    void PLUGIN_API queueClosed (Steinberg::Vst::DataExchangeUserContextID userContextID) SMTG_OVERRIDE;
    void PLUGIN_API onDataExchangeBlocksReceived (Steinberg::Vst::DataExchangeUserContextID userContextID,
                                                  Steinberg::uint32 numBlocks, Steinberg::Vst::DataExchangeBlock* blocks,
                                                  Steinberg::TBool onBackgroundThread) SMTG_OVERRIDE;

    // Editor support (main thread).
    const MeterFrame& meterFrame () const { return meter_; }
    Steinberg::uint32 meterFrameCount () const { return meterCount_; } // changes when a new frame arrives
    void clearNotches ();
    void editParameter (Steinberg::Vst::ParamID id, Steinberg::Vst::ParamValue normalized); // a whole gesture

    OBJ_METHODS (Controller, EditController)
    DEFINE_INTERFACES
        DEF_INTERFACE (Steinberg::Vst::IDataExchangeReceiver)
    END_DEFINE_INTERFACES (EditController)
    REFCOUNT_METHODS (EditController)

private:
    Steinberg::Vst::DataExchangeReceiverHandler dataExchange_ {this};
    MeterFrame meter_ {};
    Steinberg::uint32 meterCount_ {0};
};

} // namespace fwyc
