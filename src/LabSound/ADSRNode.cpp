#include "ADSRNode.h"
#include "AudioNodeInput.h"
#include "AudioNodeOutput.h"
#include "LabSound.h"
#include "AudioProcessor.h"
#include <vector>
#include <audio/VectorMath.h>

using namespace WebCore;
using namespace WebCore::VectorMath;
using namespace LabSound; 
using namespace std;

namespace LabSound {


    class ADSRNode::AdsrNodeInternal : public WebCore::AudioProcessor {
    public:

        AdsrNodeInternal(WebCore::AudioContext* context, float sampleRate)
        : AudioProcessor(sampleRate)
        , numChannels(1)
        , m_noteOffTime(0)
        , m_currentGain(0)
        {
            m_attackTime = AudioParam::create(context, "attackTime",  0.05, 0, 120);   // duration
            m_attackLevel = AudioParam::create(context, "attackLevel",  1.0, 0, 10);   // duration
            m_decayTime = AudioParam::create(context, "decayTime",   0.05,  0, 120);   // duration
            m_sustainLevel = AudioParam::create(context, "sustain", 0.75, 0, 10);   // level
            m_releaseTime = AudioParam::create(context, "release", 0.0625, 0, 120);   // duration
        }

        virtual ~AdsrNodeInternal() {
        }

        // AudioProcessor interface
        virtual void initialize() {
        }

        virtual void uninitialize() { }

        // Processes the source to destination bus.  The number of channels must match in source and destination.
        virtual void process(const WebCore::AudioBus* sourceBus, WebCore::AudioBus* destinationBus, size_t framesToProcess) {
            if (!numChannels)
                return;

            // We handle both the 1 -> N and N -> N case here.
            const float* source = sourceBus->channel(0)->data();

            // this will only ever happen once, so if heap contention is an issue it should only ever cause one glitch
            // what would be better, alloca? What does webaudio do elsewhere for this sort of thing?
            if (gainValues.size() < framesToProcess)
                gainValues.resize(framesToProcess);

            float s = m_sustainLevel->value();

            for (int i = 0; i < framesToProcess; ++i) {
                if (m_zeroSteps > 0) {
                    --m_zeroSteps;
                    m_currentGain += m_zeroStepSize;
                    gainValues[i] = m_currentGain;
                }
                else if (m_attackSteps > 0) {
                    --m_attackSteps;
                    m_currentGain += m_attackStepSize;
                    gainValues[i] = m_currentGain;
                }
                else if (m_decaySteps > 0) {
                    --m_decaySteps;
                    m_currentGain += m_decayStepSize;
                    gainValues[i] = m_currentGain;
                }
                else if (m_releaseSteps > 0) {
                    --m_releaseSteps;
                    m_currentGain += m_releaseStepSize;
                    gainValues[i] = m_currentGain;
                }
                else {
                    m_currentGain = (m_noteOffTime == DBL_MAX) ? s : 0;
                    gainValues[i] = m_currentGain;
                }
            }

            for (unsigned channelIndex = 0; channelIndex < numChannels; ++channelIndex) {
                if (sourceBus->numberOfChannels() == numChannels)
                    source = sourceBus->channel(channelIndex)->data();
                float* destination = destinationBus->channel(channelIndex)->mutableData();
                vmul(source, 1, &gainValues[0], 1, destination, 1, framesToProcess);
            }
        }

        // Resets filter state
        virtual void reset() { }

        virtual void setNumberOfChannels(unsigned i) {
            numChannels = i;
        }

        virtual double tailTime() const { return 0; }
        virtual double latencyTime() const { return 0; }

        void noteOn(double now) {
            if (m_currentGain > 0) {
                m_zeroSteps = 16;
                m_zeroStepSize = -m_currentGain / 16.0f;
            }
            else
                m_zeroSteps = 0;

            m_attackTimeTarget = now + m_attackTime->value();

            m_attackSteps = m_attackTime->value() * sampleRate();
            m_attackStepSize = m_attackLevel->value() / m_attackSteps;

            m_decayTimeTarget = m_attackTimeTarget + m_decayTime->value();

            m_decaySteps = m_decayTime->value() * sampleRate();
            m_decayStepSize = (m_sustainLevel->value() - m_attackLevel->value()) / m_decaySteps;

            m_releaseSteps = 0;

            m_noteOffTime = DBL_MAX;
        }

        void noteOff(double now) {
            // note off at any time except while a note is on, has no effect
            if (m_noteOffTime == DBL_MAX) {
                m_noteOffTime = now + m_releaseTime->value();

                m_releaseSteps = m_releaseTime->value() * sampleRate();
                m_releaseStepSize = -m_sustainLevel->value() / m_releaseSteps;
            }
        }

        int m_zeroSteps;
        float m_zeroStepSize;
        int m_attackSteps;
        float m_attackStepSize;
        int m_decaySteps;
        float m_decayStepSize;
        int m_releaseSteps;
        float m_releaseStepSize;

        int numChannels;
        double m_attackTimeTarget, m_decayTimeTarget, m_noteOffTime;
        float m_currentGain;
        std::vector<float> gainValues;
		RefPtr<AudioParam> m_attackTime;
		RefPtr<AudioParam> m_attackLevel;
		RefPtr<AudioParam> m_decayTime;
		RefPtr<AudioParam> m_sustainLevel;
		RefPtr<AudioParam> m_releaseTime;
    };
    
    AudioParam* ADSRNode::attackTime() const { return data->m_attackTime.get(); }
    AudioParam* ADSRNode::attackLevel() const { return data->m_attackLevel.get(); }
    AudioParam* ADSRNode::decayTime() const { return data->m_decayTime.get(); }
    AudioParam* ADSRNode::sustainLevel() const { return data->m_sustainLevel.get(); }
    AudioParam* ADSRNode::releaseTime() const { return data->m_releaseTime.get(); }


    void ADSRNode::set(float aT, float aL, float d, float s, float r) {
        data->m_attackTime->setValue(aT);
        data->m_attackLevel->setValue(aL);
        data->m_decayTime->setValue(d);
        data->m_sustainLevel->setValue(s);
        data->m_releaseTime->setValue(r);
    }

    void ADSRNode::noteOn() {
        data->noteOn(context()->currentTime());
    }

    void ADSRNode::noteOff() {
        data->noteOff(context()->currentTime());
    }
    
    bool ADSRNode::finished() {
        double now = context()->currentTime();
        if (now > data->m_noteOffTime) {
            data->m_noteOffTime = 0;
        }
        return now > data->m_noteOffTime;
    }

    ADSRNode::ADSRNode(WebCore::AudioContext* context, float sampleRate)
    : WebCore::AudioBasicProcessorNode(context, sampleRate)
    , data(new AdsrNodeInternal(context, sampleRate))
    {
        m_processor = std::unique_ptr<WebCore::AudioProcessor>(data);

        setNodeType((AudioNode::NodeType) LabSound::NodeTypeADSR);

        addInput(adoptPtr(new WebCore::AudioNodeInput(this)));
        addOutput(adoptPtr(new WebCore::AudioNodeOutput(this, 2))); // 2 stereo

        initialize();
    }

    ADSRNode::~ADSRNode() {
        data->numChannels = 0;
        delete data;
        data = 0;
        uninitialize();
    }

} // End namespace LabSound
