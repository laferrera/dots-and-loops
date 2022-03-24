#include "terrarium.h"
#include "daisy_petal.h"
#include "vibrato.h"
#include "scale.h"
#include "tapesaturator.h"
// #include "epiPitchShifter.h"
#include "daisysp.h"

using namespace daisy;
using namespace daisysp;
using namespace terrarium;

const int AUDIO_BLOCK_SIZE = 48;
// #define MAX_SIZE (48000 * 60 * 5) // 5 minutes of floats at 48 khz
#define MAX_SIZE (48000 * 10) // 10 seconds

DaisyPetal hw;
Led led1, led2;


bool effectBypass;
float wetMixSetting;
float sampleRateSetting;
float sampleRate;
size_t ratio;
float wetL;

float pickupTolerance = 0.25f;
float knob1,knob2, knob3, knob4, knob5, knob6;
float knob1Last, knob2Last, knob3Last, knob4Last, knob5Last, knob6Last;

bool first = true; //first loop (sets length)
bool rec = false;  //currently recording
bool play = false; //currently playing
bool inReverse = false; //are we going backwards

int pos = 0;
float DSY_SDRAM_BSS buf[MAX_SIZE];
int mod = MAX_SIZE;
int len = 0;
bool res = false; // wtf is this for

int pitchshift_semitones[] = {-31, -24, -19, -12, -7, 0, 7, 12, 19, 24, 31};
float pitchshift_factors[] = {0.125f, 0.25f, 0.375f, 0.5f, 0.75f, 1.f, 1.5f, 2.f, 3.f, 4.f, 5.f};

float sIndex; // index into buffer
uint32_t sIndexInt;
float sIndexFraction;
uint32_t sIndexRecord;
float sFactor; // how much to advance index for a new sample
float sFactorTarget; // slew to
float sFreq;   // in Hz


bool experiment = false;
int curWindow = 0;
int windowSize;
int totalWindows;
int totalRepeats = 1;
int curRepeat = 0;

void ResetBuffer();
void Controls();
void setWindows();
void setRepeats();

void NextSamples(float &output,
                 AudioHandle::InterleavingInputBuffer in,
                 size_t i);


void conditionalParamUpdate(float *myCurKnobVal, float *myLastParamSetting, float *myCurParamSetting, bool *mySettingLocked)
{
    float knobDiff = abs(*myLastParamSetting - *myCurKnobVal);
    if (*mySettingLocked)
    {
        if (knobDiff < pickupTolerance)
        {
            *mySettingLocked = false;
            // paramUpdateLedEnvelope.Trigger();
        }
    }
    else
    {
        *myCurParamSetting = *myCurKnobVal;
        *myLastParamSetting = *myCurKnobVal;
    }
}

void UpdateButtons()
{
    //switch1 pressed
    if (hw.switches[Terrarium::FOOTSWITCH_1].RisingEdge())
    {
        if (first && rec)
        {
            first = false;
            mod = len;
            len = 0;
        }

        res = true;
        play = true;
        rec = !rec;
    }

    //switch1 held
    if (hw.switches[Terrarium::FOOTSWITCH_2].TimeHeldMs() >= 1000 && res)
    {
        ResetBuffer();
        res = false;
    }

    //switch2 pressed and not empty buffer
    if (hw.switches[Terrarium::FOOTSWITCH_2].RisingEdge() && !(!rec && first))
    {
        play = !play;
        rec = false;
    }

    if (hw.switches[Terrarium::SWITCH_1].Pressed())
    {
        inReverse = true;
    } 
    else
    {
        inReverse = false;
    }

    if (hw.switches[Terrarium::SWITCH_2].Pressed())
    {
        experiment = true;
    }
    else
    {
        experiment = false;
    }
}

void Controls()
{
    hw.ProcessAllControls();

    knob1 = hw.knob[Terrarium::KNOB_1].Process();
    knob2 = hw.knob[Terrarium::KNOB_2].Process();
    knob3 = hw.knob[Terrarium::KNOB_3].Process();
    knob4 = hw.knob[Terrarium::KNOB_4].Process();
    knob5 = hw.knob[Terrarium::KNOB_5].Process();
    knob6 = hw.knob[Terrarium::KNOB_6].Process();

    wetMixSetting = knob1; //      1 is MIX no mater what
    // conditionalParamUpdate(&knob2, &wowDepth, &wowDepthLast, &wowDepthLock);                                // wowDepth = knob2;


    ////////////////////
    ///   Switches    //
    ////////////////////
    UpdateButtons();

    if (hw.switches[Terrarium::SWITCH_4].Pressed())
    {
        // compBypass = true;
    }

    led1.Set(rec);
    led2.Set(play); //this noisy af
}


void NextSamples(float &output,
                 AudioHandle::InterleavingInputBuffer in,
                 size_t i)
{

    sIndexInt = static_cast<int32_t>(sIndex);
    sIndexFraction = sIndex - sIndexInt;
    pos = sIndexInt; // lets see what happens

    if (rec)
    {
        buf[pos] = buf[pos] + in[i]; // should soft limit this
        if (first)
        {
            len++;
        }
        output = in[i] * (1 - wetMixSetting);
    }

    // output = buf[pos];
    //automatic looptime
    if (len >= MAX_SIZE)
    {
        first = false;
        mod = MAX_SIZE;
        len = 0;
        rec = false;
    }

    // get sample
    float a = buf[sIndexInt];
    float b = buf[sIndexInt + 1]; //what about edges? do a mod here
    output = a + (b - a) * sIndexFraction;
    // sIndex += sFactor;

    


    if (play)
    {

        if (experiment)
        {
            if (!inReverse)
            {
                int nextPos = pos + 1;
                if (nextPos < (curWindow + 1) * windowSize) //if still in curWindow, move on
                {
                    pos++;
                    sIndex += sFactor;
                }
                else
                {
                    if(curRepeat < totalRepeats) //back to begining of window
                    {
                        curRepeat++;
                        pos = curWindow * windowSize;
                        sIndex = curWindow * windowSize;
                    }
                    else // move onto next window
                    {
                        curRepeat = 0;
                        curWindow++;
                        pos++;
                        sIndex += sFactor;
                    }
                }
                pos %= mod;
            } 
            else
            {
                int nextPos = pos - 1;
                if (nextPos > (curWindow - 1) * windowSize) //if still in curWindow, move on
                {
                    pos--;
                    sIndex -= sFactor;
                }
                else
                {
                    if (curRepeat < totalRepeats) //back to begining of window
                    {
                        curRepeat++;
                        pos = curWindow * windowSize;
                        sIndex = curWindow * windowSize;
                    }
                    else // move onto next window
                    {
                        curRepeat = 0;
                        curWindow--;
                        pos--;
                        sIndex -= sFactor;
                    }
                }
                pos %= mod;
            }
        }
        else
        {
                if (!inReverse)
                {
                    pos++;
                    pos %= mod;
                    sIndex += sFactor; // change sFactor to negative if reverse....
                    if (sIndex > mod)
                    {
                        sIndex = 0.0f;
                    }
                }
                else
                {
                    pos--;
                    pos %= mod;
                    sIndex -= sFactor;
                    if (sIndex < 0)
                    {
                        sIndex = mod;
                    }
                }
        }
        
    }

    if (!rec)
    {
        output = output * wetMixSetting + in[i] * (1 - wetMixSetting);
    }
}

////////////////////
/// start block   //
////////////////////

static void AudioCallback(AudioHandle::InterleavingInputBuffer in,
                          AudioHandle::InterleavingOutputBuffer out,
                          size_t size)
{
    float output = 0;

    Controls();
    if (!rec)
    {
        size_t newRatio = static_cast<size_t>(floor(knob4 * 11));
        bool up = false;
        // newRatio = pitchshift_semitones[newRatio];
        if (newRatio != ratio)
        {
            ratio = newRatio;
            // sFactor = 1.f;
            sFactor = pitchshift_factors[ratio];
            // sFactorTarget = pitchshift_factors[ratio];
            // fonepole(sFactor, sFactorTarget, 0.001f);
        }
        if(knob2 != knob2Last){
            setWindows();
        }
        if (knob5 != knob5Last)
        {
            setRepeats();
        }
    }


    for (size_t i = 0; i < size; i ++)
    {
        NextSamples(output, in, i);
        // left and right outs
        out[i] = out[i + 1] = output;
    }
}

//Resets the buffer
void ResetBuffer()
{
    play = false;
    rec = false;
    first = true;
    pos = 0;
    len = 0;
    for (int i = 0; i < mod; i++)
    {
        buf[i] = 0;
    }
    mod = MAX_SIZE;
}

void setWindows(){
    knob2Last = knob2;
    int divider = static_cast<int>(floor(knob2 * 50));
    // windowSize = len / 48000 / knob2;
    windowSize = mod / divider;
    totalWindows = mod / windowSize;
    curWindow = floor(pos / windowSize);
}

void setRepeats(){
    knob5Last = knob5;
    totalRepeats = static_cast<int>(floor(knob5 * 9));
}

int main(void)
{

    hw.Init();
    hw.SetAudioBlockSize(AUDIO_BLOCK_SIZE); // this was 10 for a while
    float sample_rate = hw.AudioSampleRate();
    ResetBuffer();

    led1.Init(hw.seed.GetPin(Terrarium::LED_1), false);
    led2.Init(hw.seed.GetPin(Terrarium::LED_2), false);
    led1.Update();
    led2.Update();

    knob2Last = 0.f;
    knob5Last = 0.f;

    sIndex = 0.0f;
    sFactor = 1.f;

    hw.StartAdc();
    hw.ProcessAllControls();
    hw.StartAudio(AudioCallback);
    while (1)
    {
        // hw.DelayMs(6);
        led1.Update();
        led2.Update();
    }
}
