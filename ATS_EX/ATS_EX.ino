// ----------------------------------------------------------------------
// ATS_EX (Extended) Firmware for ATS-20 and ATS-20+ receivers.
// Based on PU2CLR sources.
// Inspired by closed-source swling.ru firmware.
// For more information check README file in my github repository:
// http://github.com/goshante/ats20_ats_ex
// ----------------------------------------------------------------------
// By Goshante
// 02.2024
// http://github.com/goshante
// ----------------------------------------------------------------------

#include <SI4735.h>
#include <EEPROM.h>
#include <Tiny4kOLED.h>
#include <PixelOperatorBold.h> 

#include "font14x24sevenSeg.h"
#include "Rotary.h"
#include "SimpleButton.h"
#include "patch_ssb_compressed.h"

#include "defs.h"
#include "globals.h"
#include "Utils.h"

void showStatus(bool cleanFreq = false);
void applyBandConfiguration(bool extraSSBReset = false);

bool isSSB()
{
    return g_currentMode > AM && g_currentMode < FM;
}

int getSteps()
{
    if (isSSB())
    {
        if (g_stepIndex >= g_amTotalSteps)
            return g_tabStep[g_stepIndex];

        return g_tabStep[g_stepIndex] * 1000;
    }

    if (g_stepIndex >= g_amTotalSteps)
        g_stepIndex = 0;

    return g_tabStep[g_stepIndex];
}

int getLastStep()
{
    if (isSSB())
        return g_amTotalSteps + g_ssbTotalSteps - 1;

    return g_amTotalSteps - 1;
}

// --------------------------
// ------- Main logic -------
// --------------------------

//Initialize controller
void setup()
{
    //We need to save more space with this
    DDRB |=  (1 << DDB5);   //13 pin
    DDRD &= ~(1 << ENCODER_PIN_A);
    PORTD |= (1 << ENCODER_PIN_A);
    DDRD &= ~(1 << ENCODER_PIN_B);
    PORTD |= (1 << ENCODER_PIN_B);
    g_voltagePinConnnected = analogRead(BATTERY_VOLTAGE_PIN) > 300;

    oled.begin(128, 64, sizeof(tiny4koled_init_128x64br), tiny4koled_init_128x64br);
    oled.clear();
    oled.on();
    oled.setFont(DEFAULT_FONT);

    //Don't use digitalRead()
    //Registers save us more space
    if (!(PINC & (1 << (ENCODER_BUTTON - 14))))
    {
        saveAllReceiverInformation();
        EEPROM.write(g_eeprom_address, g_app_id);
        
        oled.print("  EEPROM RESET");
        oled.setCursor(0, 2);
        for (uint8_t i = 0; i < 16; i++)
        {
            oled.print("-"); //Just fancy animation
            delay(60);
        }
    }
    else
    {
        oledPrint(" ATS-20 RECEIVER", 0, 0, DEFAULT_FONT, true);
        oledPrint("  ATS_EX v1.08", 0, 2);
        oledPrint(" Goshante 2024\0", 0, 4);
        oledPrint(" Best firmware", 0, 6);
        delay(2000);
    }
    oled.clear();

    //Encoder interrupts
    attachInterrupt(digitalPinToInterrupt(ENCODER_PIN_A), rotaryEncoder, CHANGE);
    attachInterrupt(digitalPinToInterrupt(ENCODER_PIN_B), rotaryEncoder, CHANGE);

    g_si4735.getDeviceI2CAddress(RESET_PIN);
    g_si4735.setup(RESET_PIN, MW_BAND_TYPE);

    delay(500);

    //Load settings from EEPROM
    if (EEPROM.read(g_eeprom_address) == g_app_id)
        readAllReceiverInformation();
    else
        saveAllReceiverInformation();

    //Clock speed configuration
    noInterrupts(); //cli()
    CLKPR = 0x80;   //Allow edit CLKPR register
    CLKPR = g_Settings[SettingsIndex::CPUSpeed].param;
    interrupts();   //sei()

    //Initialize current band settings and read frequency
    applyBandConfiguration();
    g_currentFrequency = g_previousFrequency = g_si4735.getFrequency();
    g_si4735.setVolume(g_volume);

    //Draw main screen
    oled.clear();
    showStatus();
}

uint8_t volumeEvent(uint8_t event, uint8_t pin)
{
    if (g_muteVolume)
    {
        if (!BUTTONEVENT_ISDONE(event))
        {
            if ((BUTTONEVENT_SHORTPRESS != event) || (VOLUME_BUTTON == pin))
                doVolume(1);
        }
    }

    if (!g_muteVolume)
    {
#if (0 != VOLUME_DELAY)
#if (VOLUME_DELAY > 1)
        static uint8_t count;
        if (BUTTONEVENT_FIRSTLONGPRESS == event)
        {
            count = 0;
        }
#endif
        if (BUTTONEVENT_ISLONGPRESS(event))
            if (BUTTONEVENT_LONGPRESSDONE != event)
            {
#if (VOLUME_DELAY > 1)
                if (count++ == 0)
#endif
                    doVolume(VOLUME_BUTTON == pin ? 1 : -1);
#if (VOLUME_DELAY > 1)
                count = count % VOLUME_DELAY;
#endif
            }
#else
        if (BUTTONEVENT_FIRSTLONGPRESS == event)
            event = BUTTONEVENT_SHORTPRESS;
#endif
    }
    return event;
}

uint8_t simpleEvent(uint8_t event, uint8_t pin)
{
    if (BUTTONEVENT_FIRSTLONGPRESS == event)
        event = BUTTONEVENT_SHORTPRESS;
    return event;
}

//This looks like it's better to remove them and use only simpleEvent
//But it's a part of a hack that allows us to save more flash image size
uint8_t stepEvent(uint8_t event, uint8_t pin)
{
    return simpleEvent(event, pin);
}

uint8_t agcEvent(uint8_t event, uint8_t pin)
{
    return simpleEvent(event, pin);
}

uint8_t bandEvent(uint8_t event, uint8_t pin)
{
#if (0 != BAND_DELAY)
    static uint8_t count;
    if (BUTTONEVENT_ISLONGPRESS(event) && !g_settingsActive)
    {
        if (BUTTONEVENT_LONGPRESSDONE != event)
        {
            if (BUTTONEVENT_FIRSTLONGPRESS == event)
            {
                count = 0;
            }
            if (count++ == 0)
            {
                if (BAND_BUTTON == pin)
                {
                    if (g_bandIndex < g_lastBand)
                        bandSwitch(true);
                }
                else
                {
                    if (g_bandIndex)
                        bandSwitch(false);
                }
            }
            count = count % BAND_DELAY;
        }
    }
#else
    if (BUTTONEVENT_FIRSTLONGPRESS == event)
        event = BUTTONEVENT_SHORTPRESS;
#endif
    return event;
}

// Handle encoder direction
void rotaryEncoder()
{
    uint8_t encoderStatus = g_encoder.process();
    if (encoderStatus)
        g_encoderCount = (encoderStatus == DIR_CW) ? 1 : -1;
}

//Saves more flash image size
void updateSSBCutoffFilter()
{
    // Auto mode: If SSB bandwidth 2 KHz or lower - it's better to enable cutoff filter
    if (g_Settings[SettingsIndex::CutoffFilter].param == 0)
        g_si4735.setSSBSidebandCutoffFilter((g_bandwidthSSB[g_bwIndexSSB].idx == 0 || g_bandwidthSSB[g_bwIndexSSB].idx == 4 || g_bandwidthSSB[g_bwIndexSSB].idx == 5) ? 0 : 1);
    else
        g_si4735.setSSBSidebandCutoffFilter(g_Settings[SettingsIndex::CutoffFilter].param - 1);
}

//EEPROM Save
void saveAllReceiverInformation()
{
    uint8_t addr_offset;
    EEPROM.update(g_eeprom_address, g_app_id);
    EEPROM.update(g_eeprom_address + 1, g_muteVolume > 0 ? g_muteVolume : g_si4735.getVolume());
    EEPROM.update(g_eeprom_address + 2, g_bandIndex);
    EEPROM.update(g_eeprom_address + 3, g_currentMode);
    EEPROM.update(g_eeprom_address + 4, g_currentBFO >> 8);
    EEPROM.update(g_eeprom_address + 5, g_currentBFO & 0XFF);
    EEPROM.update(g_eeprom_address + 6, g_FMStepIndex);
    EEPROM.update(g_eeprom_address + 7, g_prevMode);

    addr_offset = 8;
    g_bandList[g_bandIndex].currentFreq = g_currentFrequency;

    for (uint8_t i = 0; i <= g_lastBand; i++)
    {
        EEPROM.update(addr_offset++, (g_bandList[i].currentFreq >> 8));
        EEPROM.update(addr_offset++, (g_bandList[i].currentFreq & 0xFF));
        EEPROM.update(addr_offset++, ((g_bandList[i].bandType != FM_BAND_TYPE && g_bandList[i].currentStepIdx >= g_amTotalSteps) ? 0 : g_bandList[i].currentStepIdx));
        EEPROM.update(addr_offset++, g_bandList[i].bandwidthIdx);
    }

    for (uint8_t i = 0; i < SettingsIndex::SETTINGS_MAX; i++)
        EEPROM.update(addr_offset++, g_Settings[i].param);
}

//EEPROM Load
void readAllReceiverInformation()
{
    uint8_t addr_offset;
    int8_t bwIdx;
    g_volume = EEPROM.read(g_eeprom_address + 1);
    g_bandIndex = EEPROM.read(g_eeprom_address + 2);
    g_currentMode = EEPROM.read(g_eeprom_address + 3);
    g_currentBFO = EEPROM.read(g_eeprom_address + 4) << 8;
    g_currentBFO |= EEPROM.read(g_eeprom_address + 5);
    g_FMStepIndex = EEPROM.read(g_eeprom_address + 6);
    g_prevMode = EEPROM.read(g_eeprom_address + 7);

    addr_offset = 8;
    for (uint8_t i = 0; i <= g_lastBand; i++)
    {
        g_bandList[i].currentFreq = EEPROM.read(addr_offset++) << 8;
        g_bandList[i].currentFreq |= EEPROM.read(addr_offset++);
        g_bandList[i].currentStepIdx = EEPROM.read(addr_offset++);
        g_bandList[i].bandwidthIdx = EEPROM.read(addr_offset++);
    }

    for (uint8_t i = 0; i < SettingsIndex::SETTINGS_MAX; i++)
        g_Settings[i].param = EEPROM.read(addr_offset++);

    oled.setContrast(uint8_t(g_Settings[SettingsIndex::Brightness].param) * 2);

    g_previousFrequency = g_currentFrequency = g_bandList[g_bandIndex].currentFreq;
    if (g_bandList[g_bandIndex].bandType == FM_BAND_TYPE)
        g_FMStepIndex = g_bandList[g_bandIndex].currentStepIdx;
    else
        g_stepIndex = g_bandList[g_bandIndex].currentStepIdx;
    bwIdx = g_bandList[g_bandIndex].bandwidthIdx;
    if (g_stepIndex >= g_amTotalSteps)
        g_stepIndex = 0;

    if (isSSB())
    {
        loadSSBPatch();
        g_bwIndexSSB = (bwIdx > 5) ? 5 : bwIdx;
        g_si4735.setSSBAudioBandwidth(g_bandwidthSSB[g_bwIndexSSB].idx);
        updateSSBCutoffFilter();
    }
    else if (g_currentMode == AM)
    {
        g_bwIndexAM = bwIdx;
        g_si4735.setBandwidth(g_bandwidthAM[g_bwIndexAM].idx, 1);
    }
    else
    {
        g_bwIndexFM = bwIdx;
        g_si4735.setFmBandwidth(g_bandwidthFM[g_bwIndexFM].idx);
    }

    applyBandConfiguration();
}

//For saving features
void resetEepromDelay()
{
    g_storeTime = millis();
    g_previousFrequency = 0;
}

//Draw frequency. 
//BFO and main frequency produce actual frequency that is displayed on LCD
//Too sensitive logic, do not change
void showFrequency(bool cleanDisplay = false)
{
    if (g_settingsActive)
        return;

    char unit[4];
    char freqDisplay[7];
    char ssbSuffix[4];
    static uint8_t prevLen = 0;
    uint16_t khzBFO, tailBFO;
    uint8_t off = (isSSB() ? -5 : 4) + 8;

    unit[0] = 'K';
    unit[1] = 'H';
    unit[2] = 'z';
    unit[3] = 0x0;

    ssbSuffix[0] = '.';
    ssbSuffix[1] = '0';
    ssbSuffix[2] = '0';
    ssbSuffix[3] = '\0';

    if (g_bandList[g_bandIndex].bandType == FM_BAND_TYPE)
    {
        convertToChar(freqDisplay, g_currentFrequency, 5, 3, '.', '/');
        unit[0] = 'M';
    }
    else
    {
        if (!isSSB())
        {
            bool swMhz = g_Settings[SettingsIndex::SWUnits].param == 1;
            convertToChar(freqDisplay, g_currentFrequency, 5, (g_bandList[g_bandIndex].bandType == SW_BAND_TYPE && swMhz) ? 2 : 0, '.', '/');
            if (g_bandList[g_bandIndex].bandType == SW_BAND_TYPE && swMhz)
                unit[0] = 'M';
        }
        else
        {
            splitFreq(khzBFO, tailBFO);
            //utoa(freqDisplay, khzBFO);
            convertToChar(freqDisplay, khzBFO, ilen(khzBFO));
        }
    }

    uint8_t len = isSSB() ? ilen(khzBFO) : ilen(g_currentFrequency);
    if (cleanDisplay)
    {
        oled.setCursor(0, 3);
        oledPrint("/////////", 0, 3, FONT14X24SEVENSEG); // This character is an empty space in my seven seg font.
    }
    else if (isSSB() && len > prevLen && len == 5)
        oledPrint("   ", 102, 4, DEFAULT_FONT);

    oledPrint(freqDisplay, off, 3, FONT14X24SEVENSEG);

    if (isSSB())
    {
        //utoa((ilen(tailBFO) == 1) ? &ssbSuffix[2] : &ssbSuffix[1], tailBFO);
        convertToChar((ilen(tailBFO) == 1) ? &ssbSuffix[2] : &ssbSuffix[1], tailBFO, ilen(tailBFO));
        ssbSuffix[3] = 0;
        oledPrint(ssbSuffix);
        if (len != prevLen && len < prevLen)
            oledPrint("/");
    }

    if (!isSSB() || isSSB() && len < 5)
        oledPrint(unit, 102, 4, DEFAULT_FONT);
        
    prevLen = len;
}

//This function is called by station seek logic
void showFrequencySeek(uint16_t freq)
{
    g_currentFrequency = freq;
    showFrequency();
}

bool checkStopSeeking()
{
    return (bool)g_encoderCount || !(PINC & (1 << (ENCODER_BUTTON - 14)));
}

//Update and draw main screen UI. 
//basicUpdate - update minimum as possible
//cleanFreq   - force clean frequency line
void showStatus(bool cleanFreq)
{
    showFrequency(cleanFreq);
    showModulation();
    showStep();
    showBandwidth();
    showCharge(true);
    showVolume();
}

void updateLowerDisplayLine()
{
    oledPrint(_literal_EmptyLine, 0, 6, DEFAULT_FONT);
    showModulation();
    showStep();
    showCharge(true);
}

//Converts settings value to UI value
void SettingParamToUI(char* buf, uint8_t idx)
{
    switch (g_Settings[idx].type)
    {
    case SettingType::ZeroAuto:
        if (g_Settings[idx].param == 0)
        {
            buf[0] = 'A';
            buf[1] = 'U';
            buf[2] = 'T';
            buf[3] = 0x0;
        }
        else
            convertToChar(buf, g_Settings[idx].param, 3);

        break;

    case SettingType::Num:
        convertToChar(buf, g_Settings[idx].param, 3);
        break;

    case SettingType::SwitchAuto:
        if (g_Settings[idx].param == 0)
        {
            buf[0] = 'A';
            buf[1] = 'U';
            buf[2] = 'T';       }
        else if (g_Settings[idx].param == 1)
        {
            buf[0] = 'O';
            buf[1] = 'n';
            buf[2] = ' ';
        }
        else
        {
            buf[0] = 'O';
            buf[1] = 'f';
            buf[2] = 'f';
        }
        buf[3] = 0x0;
        break;

    case SettingType::Switch:
        if (idx == SettingsIndex::DeEmp)
        {
            if (g_Settings[idx].param == 0)
            {
                buf[0] = '5';
                buf[1] = '0';
                buf[2] = 'u';
            }
            else
            {
                buf[0] = '7';
                buf[1] = '5';
                buf[2] = 'u';
            }
        }
        else if (idx == SettingsIndex::SWUnits)
        {
            if (g_Settings[idx].param == 0)
                buf[0] = 'K';
            else
                buf[0] = 'M';
            buf[1] = 'H';
            buf[2] = 'z';
        }
        else if (idx == SettingsIndex::SSM)
        {
            if (g_Settings[idx].param == 0)
            {
                buf[0] = 'R';
                buf[1] = 'S';
                buf[2] = 'S';
            }
            else
            {
                buf[0] = 'S';
                buf[1] = 'N';
                buf[2] = 'R';
            }
        }
        else if (idx == SettingsIndex::CPUSpeed)
        {
            if (g_Settings[idx].param == 0)
            {
                buf[0] = '1';
                buf[1] = '0';
                buf[2] = '0';
            }
            else
            {
                buf[0] = '5';
                buf[1] = '0';
                buf[2] = '%';
            }
        }
        else
        {
            if (g_Settings[idx].param == 0)
            {
                buf[0] = 'O';
                buf[1] = 'f';
                buf[2] = 'f';
            }
            else
            {
                buf[0] = 'O';
                buf[1] = 'n';
                buf[2] = ' ';
            }
        }
        buf[3] = 0x0;
        break;
    }
}

// If full false - update only value
void DrawSetting(uint8_t idx, bool full)
{
    if (!g_settingsActive)
        return;

    char buf[5];
    uint8_t place = idx - ((g_SettingsPage - 1) * 6);
    uint8_t yOffset = place > 2 ? (place - 3) * 2 : place * 2;
    uint8_t xOffset = place > 2 ? 60 : 0;
    if (full)
        oledPrint(g_Settings[idx].name, 5 + xOffset, 2 + yOffset, DEFAULT_FONT, idx == g_SettingSelected && !g_SettingEditing);
    SettingParamToUI(buf, idx);
    oledPrint(buf, 35 + xOffset, 2 + yOffset, DEFAULT_FONT, idx == g_SettingSelected && g_SettingEditing);
}

//Update and draw settings UI
void showSettings()
{
    for (uint8_t i = 0; i < 6 && i + ((g_SettingsPage - 1) * 6) < SettingsIndex::SETTINGS_MAX; i++)
        DrawSetting(i + ((g_SettingsPage - 1) * 6), true);
}

void showSettingsTitle()
{
    oledPrint("   SETTINGS  ", 0, 0, DEFAULT_FONT, true);
    oled.invertOutput(true);
    oled.print(uint8_t(g_SettingsPage));
    oled.print("/");
    oled.print(uint8_t(g_SettingsMaxPages));
    oled.invertOutput(false);
}

void switchSettingsPage()
{
    g_SettingsPage++;
    if (g_SettingsPage > g_SettingsMaxPages)
        g_SettingsPage = 1;

    g_SettingSelected = 6 * (g_SettingsPage - 1);
    g_SettingEditing = false;
    oled.clear();
    showSettingsTitle();
    showSettings();
}

//Switch between main screen and settings mode
void switchSettings()
{
    oled.clear();
    if (!g_settingsActive)
    {
        g_settingsActive = true;
        g_SettingsPage = 1;
        showSettingsTitle();
        g_SettingSelected = 0;
        g_SettingEditing = false;
        showSettings();
    }
    else
    {
        g_settingsActive = false;
        showStatus();
    }
}

//Draw curremt modulation
void showModulation()
{
    oledPrint(g_bandModeDesc[g_currentMode], 0, 0, DEFAULT_FONT, g_cmdBand && g_currentMode == FM);
    oled.print(" ");
    if (isSSB() && g_Settings[SettingsIndex::Sync].param == 1)
        oledPrint("S", -1, -1, LastFont, true);
    else
        oled.print(" ");

    showBandTag();
}

//Draw current band
void showBandTag()
{
    if (g_sMeterOn || g_displayRDS)
        return;

    oledPrint(g_bandIndex == 19 ? "CB" : bandTags[g_bandList[g_bandIndex].bandType], 0, 6, DEFAULT_FONT, g_cmdBand && g_currentMode != FM);
}

//Draw volume level
void showVolume()
{
    if (g_settingsActive)
        return;

    char buf[3];
    if (g_muteVolume == 0)
        convertToChar(buf, g_si4735.getCurrentVolume(), 2, 0, 0);
    else
    {
        buf[0] = ' ';
        buf[1] = 'M';
        buf[2] = 0;
    }

    oledPrint(buf, (128 - (8 * 2) + 2 - 6), 0, DEFAULT_FONT, g_cmdVolume);
}

//Draw battery charge
//This feature requires hardware mod
//Voltage divider made of two 10 KOhm resistors between + and GND of Li-Ion battery
//Solder it to A2 analog pin
void showCharge(bool forceShow)
{
    if (!g_voltagePinConnnected)
        return;

    //This values represent voltage values in ATMega328p analog units with reference voltage 3.30v
    //Voltage pin reads voltage from voltage divider, so it have to be 1/2 of Li-Ion battery voltage
    const uint16_t chargeFull = 651;    //2.1v
    const uint16_t chargeLow = 558;     //1.8v
    static uint32_t lastChargeShow = 0;
    static uint16_t averageVoltageSamples = analogRead(BATTERY_VOLTAGE_PIN);

    if ((millis() - lastChargeShow) > 10000 || lastChargeShow == 0 || forceShow)
    {
        char buf[4];
        buf[3] = 0;
        int16_t percents = (((averageVoltageSamples - chargeLow) * 100) / (chargeFull - chargeLow));
        bool isUsbCable = percents > 120;

        if (percents >= 100)
            percents = 100;

        else if (percents < 0)
            percents = 0;

        if (!isUsbCable)
            convertToChar(buf, percents, ilen(percents));
        else
            buf[0] = '\0';

        if (ilen(percents) < 3)
            buf[2] = '%';


        if (!g_settingsActive && !g_sMeterOn && !g_displayRDS)
            oledPrint(buf, 102, 6, DEFAULT_FONT);
        lastChargeShow = millis();
        averageVoltageSamples = analogRead(BATTERY_VOLTAGE_PIN);
    }
    else
    {
        if (averageVoltageSamples > 0)
            averageVoltageSamples = (averageVoltageSamples + analogRead(BATTERY_VOLTAGE_PIN)) / 2;
        else
            averageVoltageSamples = analogRead(BATTERY_VOLTAGE_PIN);
    }  
}

void showRDS()
{
    static uint16_t lastUpdatedFreq = 0;
    static uint32_t lastUpdatedTime = millis();
    static bool succeed = false;

    if (g_currentMode != FM || !g_displayRDS || g_settingsActive)
    { 
        lastUpdatedFreq = 0;
        g_rdsPrevLen = 0;
        succeed = false;
        g_rdsActiveInfo = 0;
        return;
    }

    if (millis() - lastUpdatedTime > 300)
        succeed = false;

    if (lastUpdatedFreq != g_currentFrequency || g_rdsSwitchPressed)
    {
        if (g_rdsSwitchPressed)
        {
            g_rdsActiveInfo++;
            if (g_rdsActiveInfo > RDSActiveInfo::ProgramInfo)
                g_rdsActiveInfo = RDSActiveInfo::StationName;
        }
        else
        {
            g_rdsActiveInfo = RDSActiveInfo::StationName;
            succeed = false;
        }
        g_rdsPrevLen = 0;
        oledPrint(_literal_EmptyLine, 0, 6, DEFAULT_FONT);
    }
    lastUpdatedFreq = g_currentFrequency;


    if (!succeed)
        g_si4735.getRdsStatus();

    if (!succeed && g_si4735.getRdsReceived() && g_si4735.getRdsSync() && g_si4735.getNumRdsFifoUsed() > 1)
    {
        g_RDSCells[RDSActiveInfo::StationName] = g_si4735.getRdsStationName();
        g_RDSCells[RDSActiveInfo::StationInfo] = g_si4735.getRdsStationInformation();
        g_RDSCells[RDSActiveInfo::ProgramInfo] = g_si4735.getRdsProgramInformation();
        g_RDSCells[RDSActiveInfo::StationInfo][17] = '\0';
        g_RDSCells[RDSActiveInfo::ProgramInfo][17] = '\0';
        succeed = true;
        lastUpdatedTime = millis();
    }
    else if (!g_rdsSwitchPressed && succeed)
        return;

    uint8_t len = strlen8(g_RDSCells[g_rdsActiveInfo]);

    if (len == 0 && !g_rdsSwitchPressed)
        return;

    oledPrint(g_RDSCells[g_rdsActiveInfo], 0, 6, DEFAULT_FONT);
    
    uint8_t toPrint = len == 0 ? 3 : (len < g_rdsPrevLen ? min(g_rdsPrevLen - len, 16 - len) : 0);
    char printChar = len == 0 ? '.' : ' ';
    for (uint8_t i = 0; i < toPrint; i++) 
        oled.print(printChar);

    g_rdsPrevLen = len;
    g_rdsSwitchPressed = false;
}

//Draw steps (with units)
void showStep()
{
    if (g_sMeterOn || g_displayRDS)
        return;

    char buf[5];
    if (g_currentMode == FM)
    {
        if (g_tabStepFM[g_FMStepIndex] == 100)
        {
            buf[0] = ' ';
            buf[1] = ' ';
            buf[2] = '1';
            buf[3] = 'M';
            buf[4] = 0x0;
        }
        else
        {
            convertToChar(buf, g_tabStepFM[g_FMStepIndex] * 10, 3);
            buf[3] = 'k';
            buf[4] = '\0';
        }
    }
    else
    {
        if (g_tabStep[g_stepIndex] == 1000)
        {
            buf[0] = ' ';
            buf[1] = ' ';
            buf[2] = '1';
            buf[3] = 'M';
            buf[4] = 0x0;
        }
        else if (isSSB() && g_stepIndex >= g_amTotalSteps)
            convertToChar(buf, g_tabStep[g_stepIndex], 4);
        else
        {
            convertToChar(buf, g_tabStep[g_stepIndex], 3);
            buf[3] = 'k';
            buf[4] = '\0';
        }
    }

    uint8_t off = 50;
    oledPrint("St:", off - 16, 6, DEFAULT_FONT, g_cmdStep);
    oledPrint(buf, off + 8, 6, DEFAULT_FONT, g_cmdStep);
}

void showSMeter()
{
    static uint32_t sMeterUpdated = 0;
    if (millis() - sMeterUpdated < 100)
        return;

    g_si4735.getCurrentReceivedSignalQuality();
    uint8_t rssi = g_si4735.getCurrentRSSI();
    rssi = rssi > 64 ? 64 : rssi;

    int sMeterValue = rssi / (64 / 16);
    char buf[17];
    for (uint8_t i = 0; i < sizeof(buf) - 1; i++)
    {
        if (i < sMeterValue)
            buf[i] = '|';
        else
            buf[i] = ' ';
    }
    buf[sizeof(buf) - 1] = 0x0;

    oledPrint(buf, 0, 6, DEFAULT_FONT);
    sMeterUpdated = millis();
}

//Draw bandwidth (Ignored for CW mode)
void showBandwidth()
{
    char* bw;
    if (isSSB())
    {
        bw = (char*)g_bandwidthSSB[g_bwIndexSSB].desc;
        if (g_currentMode == CW)
            bw = "    ";
    }
    else if (g_currentMode == AM)
    {
        bw = (char*)g_bandwidthAM[g_bwIndexAM].desc;
    }
    else
    {
        bw = (char*)g_bandwidthFM[g_bwIndexFM].desc;
    }

    oledPrint(bw, 45, 0, DEFAULT_FONT, g_cmdBw);
}

void bandSwitch(bool up)
{
    g_bandList[g_bandIndex].currentFreq = g_currentFrequency;

    if (g_currentMode == FM)
        g_bandList[g_bandIndex].currentStepIdx = g_FMStepIndex;
    else
        g_bandList[g_bandIndex].currentStepIdx = g_stepIndex;

    if (up)
    {
        if (g_bandIndex < g_lastBand)
            g_bandIndex++;
        else
            g_bandIndex = 0;
    }
    else
    {
        if (g_bandIndex > 0)
            g_bandIndex--;
        else
            g_bandIndex = g_lastBand;
    }

    if (g_sMeterOn)
    {
        g_sMeterOn = false;
        oledPrint(_literal_EmptyLine, 0, 6, DEFAULT_FONT);
    }

    if (g_displayRDS && g_currentMode != FM)
    {
        g_displayRDS = false;
        oledPrint(_literal_EmptyLine, 0, 6, DEFAULT_FONT);
    }

    g_currentBFO = 0;
    if (isSSB())
        g_si4735.setSSBBfo(0);
    applyBandConfiguration();
}

// This function is required for using SSB. Si473x controllers do not support SSB by-default.
// But we can patch internal RAM of Si473x with special patch to make it work in SSB mode.
// Patch must be applied every time we enable SSB after AM or FM.
void loadSSBPatch()
{
    // This works, but i am not sure it's safe
    //g_si4735.setI2CFastModeCustom(700000);
    g_si4735.setI2CFastModeCustom(500000);
    g_si4735.queryLibraryId(); //Do we really need this? Research it.
    g_si4735.patchPowerUp();
    delay(50);
    g_si4735.downloadCompressedPatch(ssb_patch_content, sizeof(ssb_patch_content), cmd_0x15, sizeof(cmd_0x15));
    g_si4735.setSSBConfig(g_bandwidthSSB[g_bwIndexSSB].idx, 1, 0, 1, 0, 1);
    g_si4735.setI2CStandardMode();
    g_ssbLoaded = true;
    g_stepIndex = 0;
}

void setRDSConfig(uint8_t bias)
{
    g_si4735.setRdsConfig(1, bias, bias, bias, bias);
}

//Update receiver settings after changing band and modulation
void applyBandConfiguration(bool extraSSBReset = false)
{
    if (g_bandList[g_bandIndex].bandType == FM_BAND_TYPE)
    {
        g_currentMode = FM;
        g_si4735.setTuneFrequencyAntennaCapacitor(0);
        g_si4735.setFM(g_bandList[g_bandIndex].minimumFreq,
            g_bandList[g_bandIndex].maximumFreq,
            g_bandList[g_bandIndex].currentFreq,
            g_tabStepFM[g_bandList[g_bandIndex].currentStepIdx]);
        g_si4735.setSeekFmLimits(g_bandList[g_bandIndex].minimumFreq, g_bandList[g_bandIndex].maximumFreq);
        g_si4735.setSeekFmSpacing(1);
        g_ssbLoaded = false;
        setRDSConfig(g_Settings[SettingsIndex::RDSError].param);
        g_si4735.setFifoCount(1);
        g_bwIndexFM = g_bandList[g_bandIndex].bandwidthIdx;
        g_si4735.setFmBandwidth(g_bandwidthFM[g_bwIndexFM].idx);
        g_si4735.setFMDeEmphasis(g_Settings[SettingsIndex::DeEmp].param == 0 ? 1 : 2);
    }
    else
    {
        if (g_bandList[g_bandIndex].bandType == MW_BAND_TYPE || g_bandList[g_bandIndex].bandType == LW_BAND_TYPE)
            g_si4735.setTuneFrequencyAntennaCapacitor(0);
        else
            g_si4735.setTuneFrequencyAntennaCapacitor(1);

        uint16_t minFreq = g_bandList[g_bandIndex].minimumFreq;
        uint16_t maxFreq = g_bandList[g_bandIndex].maximumFreq;
        if (g_bandList[g_bandIndex].bandType == SW_BAND_TYPE)
        {
            minFreq = SW_LIMIT_LOW;
            maxFreq = SW_LIMIT_HIGH;
        }

        if (g_ssbLoaded)
        {
            if (g_prevMode == AM || g_prevMode == FM)
                g_currentBFO = 0;
            if (extraSSBReset)
                loadSSBPatch();

            //Call this before to call crazy volume after AM when SVC is off
            g_si4735.setSSBAutomaticVolumeControl(g_Settings[SettingsIndex::SVC].param);
            g_si4735.setSSB(minFreq,
                maxFreq,
                g_bandList[g_bandIndex].currentFreq,
                g_bandList[g_bandIndex].currentStepIdx >= g_amTotalSteps ? 0 : g_tabStep[g_bandList[g_bandIndex].currentStepIdx],
                g_currentMode == CW ? LSB : g_currentMode);
            updateSSBCutoffFilter();
            g_si4735.setSSBAutomaticVolumeControl(g_Settings[SettingsIndex::SVC].param);
            g_si4735.setSSBDspAfc(g_Settings[SettingsIndex::Sync].param == 1 ? 0 : 1);
            g_si4735.setSSBAvcDivider(g_Settings[SettingsIndex::Sync].param == 0 ? 0 : 3); //Set Sync mode
            g_si4735.setAmSoftMuteMaxAttenuation(g_Settings[SettingsIndex::SoftMute].param);
            g_bwIndexSSB = g_bandList[g_bandIndex].bandwidthIdx;
            g_si4735.setSSBAudioBandwidth(g_currentMode == CW ? g_bandwidthSSB[0].idx : g_bandwidthSSB[g_bwIndexSSB].idx);
            g_si4735.setSSBBfo(g_currentBFO * -1);
            g_si4735.setSSBSoftMute(g_Settings[SettingsIndex::SSM].param);
        }
        else
        {
            g_currentMode = AM;
            g_bandList[g_bandIndex].currentFreq = (g_bandList[g_bandIndex].currentFreq < LW_LIMIT_LOW) ? LW_LIMIT_LOW : g_bandList[g_bandIndex].currentFreq;
            g_si4735.setAM(minFreq,
                maxFreq,
                g_bandList[g_bandIndex].currentFreq,
                g_bandList[g_bandIndex].currentStepIdx >= g_amTotalSteps ? 0 : g_tabStep[g_bandList[g_bandIndex].currentStepIdx]);
            g_si4735.setAmSoftMuteMaxAttenuation(g_Settings[SettingsIndex::SoftMute].param);
            g_bwIndexAM = g_bandList[g_bandIndex].bandwidthIdx;
            g_si4735.setBandwidth(g_bandwidthAM[g_bwIndexAM].idx, 1);
        }

        agcSetFunc();
        g_si4735.setAvcAmMaxGain(g_Settings[SettingsIndex::AutoVolControl].param);
        g_si4735.setSeekAmLimits(minFreq, maxFreq);
        g_si4735.setSeekAmSpacing((g_tabStep[g_bandList[g_bandIndex].currentStepIdx] > 10 || g_bandList[g_bandIndex].currentStepIdx >= g_amTotalSteps) ? 10 : g_tabStep[g_bandList[g_bandIndex].currentStepIdx]);
    }

    g_currentFrequency = g_bandList[g_bandIndex].currentFreq;
    if (g_currentMode == FM)
        g_FMStepIndex = g_bandList[g_bandIndex].currentStepIdx;
    else
        g_stepIndex = g_bandList[g_bandIndex].currentStepIdx;

    if ((g_bandList[g_bandIndex].bandType == LW_BAND_TYPE || g_bandList[g_bandIndex].bandType == MW_BAND_TYPE)
        && g_stepIndex > g_amTotalStepsSSB)
        g_stepIndex = g_amTotalStepsSSB;

    if (!g_settingsActive)
        showStatus(true);
    resetEepromDelay();
}

//Step value regulation
void doStep(int8_t v)
{
    if (g_currentMode == FM)
    {
        g_FMStepIndex = (v == 1) ? g_FMStepIndex + 1 : g_FMStepIndex - 1;
        if (g_FMStepIndex > g_lastStepFM)
            g_FMStepIndex = 0;
        else if (g_FMStepIndex < 0)
            g_FMStepIndex = g_lastStepFM;

        g_si4735.setFrequencyStep(g_tabStepFM[g_FMStepIndex]);
        g_bandList[g_bandIndex].currentStepIdx = g_FMStepIndex;
        //g_si4735.setSeekFmSpacing(g_tabStepFM[g_FMStepIndex]);
        showStep();
    }
    else
    {
        g_stepIndex = (v == 1) ? g_stepIndex + 1 : g_stepIndex - 1;
        if (g_stepIndex > getLastStep())
            g_stepIndex = 0;
        else if (g_stepIndex < 0)
            g_stepIndex = getLastStep();

        //SSB Step limit
        else if (isSSB() && v == 1 && g_stepIndex >= g_amTotalStepsSSB && g_stepIndex < g_amTotalSteps)
            g_stepIndex = g_amTotalSteps;
        else if (isSSB() && v != 1 && g_stepIndex >= g_amTotalStepsSSB && g_stepIndex < g_amTotalSteps)
            g_stepIndex = g_amTotalStepsSSB - 1;
        
        //LW/MW Step limit
        else if ((g_bandList[g_bandIndex].bandType == LW_BAND_TYPE || g_bandList[g_bandIndex].bandType == MW_BAND_TYPE)
            && v == 1 && g_stepIndex > g_amTotalStepsSSB && g_stepIndex < g_amTotalSteps)
            g_stepIndex = g_amTotalSteps;
        else if ((g_bandList[g_bandIndex].bandType == LW_BAND_TYPE || g_bandList[g_bandIndex].bandType == MW_BAND_TYPE)
            && v != 1 && g_stepIndex > g_amTotalStepsSSB && g_stepIndex < g_amTotalSteps)
            g_stepIndex = g_amTotalStepsSSB;

        if (!isSSB() || isSSB() && g_stepIndex < g_amTotalSteps)
        {
            g_si4735.setFrequencyStep(g_tabStep[g_stepIndex]);
            g_bandList[g_bandIndex].currentStepIdx = g_stepIndex;
            g_si4735.setSeekAmSpacing((g_tabStep[g_stepIndex] > 10) ? 10 : g_tabStep[g_stepIndex]);
        }
        showStep();
    }
}

//Volume control
void doVolume(int8_t v)
{
    if (g_muteVolume)
    {
        g_si4735.setVolume(g_muteVolume);
        g_muteVolume = 0;
    }
    else
    {
        if (v == 1)
            g_si4735.volumeUp();
        else
            g_si4735.volumeDown();
    }
    showVolume();
}

//Helps to save more flash image size
void doSwitchLogic(int8_t& param, int8_t low, int8_t high, int8_t step)
{
    param += step;
    if (param < low)
        param = high;
    else if (param > high)
        param = low;
}

void agcSetFunc()
{
    uint8_t att = g_Settings[SettingsIndex::ATT].param;
    uint8_t disableAgc = att > 0;
    uint8_t agcNdx;
    if (att > 1) 
        agcNdx = att - 1;
    else
        agcNdx = 0;
    g_si4735.setAutomaticGainControl(disableAgc, agcNdx);
}

//Settings: Attenuation
void doAttenuation(int8_t v)
{
    doSwitchLogic(g_Settings[SettingsIndex::ATT].param, 0, 37, v);
    agcSetFunc();
}

//Settings: Soft Mute
void doSoftMute(int8_t v)
{
    doSwitchLogic(g_Settings[SettingsIndex::SoftMute].param, 0, 32, v);

    if (g_currentMode != FM)
        g_si4735.setAmSoftMuteMaxAttenuation(g_Settings[SettingsIndex::SoftMute].param);
}

//Settings: Brightness
void doBrightness(int8_t v)
{
    doSwitchLogic(g_Settings[SettingsIndex::Brightness].param, 5, 125, v);
    oled.setContrast(uint8_t(g_Settings[SettingsIndex::Brightness].param) * 2);
}

//Settings: SSB AVC Switch
void doSSBAVC(int8_t v = 0)
{
    doSwitchLogic(g_Settings[SettingsIndex::SVC].param, 0, 1, v);

    if (isSSB())
    {
        g_si4735.setSSBAutomaticVolumeControl(g_Settings[SettingsIndex::SVC].param);
        g_bandList[g_bandIndex].currentFreq = g_currentFrequency;
        applyBandConfiguration(true);
    }
}

//Settings: Automatic Volume Control
void doAvc(int8_t v)
{
    doSwitchLogic(g_Settings[SettingsIndex::AutoVolControl].param, 12, 90, 2 * v);

    if (g_currentMode != FM)
        g_si4735.setAvcAmMaxGain(g_Settings[SettingsIndex::AutoVolControl].param);
}

//Settings: Sync switch
void doSync(int8_t v = 0)
{
    doSwitchLogic(g_Settings[SettingsIndex::Sync].param, 0, 1, v);

    if (isSSB())
    {
        g_si4735.setSSBDspAfc(g_Settings[SettingsIndex::Sync].param == 1 ? 0 : 1);
        g_si4735.setSSBAvcDivider(g_Settings[SettingsIndex::Sync].param == 0 ? 0 : 3); //Set Sync mode
        g_bandList[g_bandIndex].currentFreq = g_currentFrequency;
        applyBandConfiguration(true);
    }
}

//Settings: FM DeEmp switch (50 or 75)
void doDeEmp(int8_t v = 0)
{
    doSwitchLogic(g_Settings[SettingsIndex::DeEmp].param, 0, 1, v);

    if (g_currentMode == FM)
        g_si4735.setFMDeEmphasis(g_Settings[SettingsIndex::DeEmp].param == 0 ? 1 : 2);
}

//Settings: SW Units
void doSWUnits(int8_t v = 0)
{
    doSwitchLogic(g_Settings[SettingsIndex::SWUnits].param, 0, 1, v);
}

//Settings: SW Units
void doSSBSoftMuteMode(int8_t v = 0)
{
    doSwitchLogic(g_Settings[SettingsIndex::SSM].param, 0, 1, v);

    if (isSSB())
        g_si4735.setSSBSoftMute(g_Settings[SettingsIndex::SSM].param);
}

//Settings: SSB Cutoff filter
void doCutoffFilter(int8_t v)
{
    doSwitchLogic(g_Settings[SettingsIndex::CutoffFilter].param, 0, 2, v);

    if (isSSB())
        updateSSBCutoffFilter();
}

//Settings: CPU Frequency divider
void doCPUSpeed(int8_t v = 0)
{
    doSwitchLogic(g_Settings[SettingsIndex::CPUSpeed].param, 0, 1, v);

    noInterrupts();
    CLKPR = 0x80;
    CLKPR = g_Settings[SettingsIndex::CPUSpeed].param;
    interrupts();
}

//Settings: RDS Error Level
void doRDSErrorLevel(int8_t v)
{
    doSwitchLogic(g_Settings[SettingsIndex::RDSError].param, 0, 3, v);

    if (g_currentMode == FM)
        setRDSConfig(g_Settings[SettingsIndex::RDSError].param);
}

void doRDS()
{
    g_displayRDS = !g_displayRDS;

    if (g_displayRDS)
    {
        g_sMeterOn = false;
        oledPrint(_literal_EmptyLine, 0, 6, DEFAULT_FONT);
        g_si4735.getRdsStatus();
        showRDS();
    }
    else
        updateLowerDisplayLine();
}

//Prevents repeatable code for flash image size saving
void doBandwidthLogic(int8_t& bwIndex, uint8_t upperLimit, uint8_t v)
{
    bwIndex = (v == 1) ? bwIndex + 1 : bwIndex - 1;

    if (bwIndex > upperLimit)
        bwIndex = 0;
    else if (bwIndex < 0)
        bwIndex = upperLimit;

    g_bandList[g_bandIndex].bandwidthIdx = bwIndex;
}

//Bandwidth regulation logic
void doBandwidth(uint8_t v)
{
    if (isSSB())
    {
        doBandwidthLogic(g_bwIndexSSB, 5, v);
        g_si4735.setSSBAudioBandwidth(g_bandwidthSSB[g_bwIndexSSB].idx);
        updateSSBCutoffFilter();
    }
    else if (g_currentMode == AM)
    {
        doBandwidthLogic(g_bwIndexAM, g_maxFilterAM, v);
        g_bandList[g_bandIndex].bandwidthIdx = g_bwIndexAM;
        g_si4735.setBandwidth(g_bandwidthAM[g_bwIndexAM].idx, 1);
    }
    else
    {
        doBandwidthLogic(g_bwIndexFM, 4, v);
        g_bandList[g_bandIndex].bandwidthIdx = g_bwIndexFM;
        g_si4735.setFmBandwidth(g_bandwidthFM[g_bwIndexFM].idx);
    }
    showBandwidth();
}

void disableCommand(bool* b, bool value, void (*showFunction)())
{
    static bool anyOn = false;
    if (anyOn)
    {
        g_cmdVolume = false;
        g_cmdStep = false;
        g_cmdBw = false;
        g_cmdBand = false;
        showVolume();
        showStep();
        showBandwidth();
        showModulation();

        anyOn = false;

    }
    if (b != NULL)
        *b = anyOn = value;
    if (showFunction != NULL)
        showFunction();
}

bool clampSSBBand()
{
    int freq = g_currentFrequency + (g_currentBFO / 1000);
    auto bfoReset = [&]()
    {
        g_currentBFO = 0;
        g_si4735.setSSBBfo(0);
        showFrequency(true);
        showModulation();
    };

    //Special SSB limit for LW
    if (freq <= LW_LIMIT_LOW_SSB)
    {
        g_currentFrequency = LW_LIMIT_LOW;
        g_bandList[0].currentFreq = LW_LIMIT_LOW;
        g_si4735.setFrequency(LW_LIMIT_LOW);
        bfoReset();
        return false;
    }
    else if (freq < LW_LIMIT_LOW)
        return false;

    if (freq > g_bandList[g_bandIndex].maximumFreq)
    {
        if (g_bandList[g_bandIndex + 1].bandType == FM_BAND_TYPE)
        {
            g_bandIndex = 0;
            g_currentFrequency = g_bandList[g_bandIndex].minimumFreq;
            g_bandList[g_bandIndex].currentFreq = g_currentFrequency;
            g_si4735.setFrequency(g_bandList[g_bandIndex].minimumFreq);
            bfoReset();
            return true;
        }
        else
        {
            g_bandIndex++;
            showModulation();
        }
    }
    else if (freq < g_bandList[g_bandIndex].minimumFreq)
    {
        if (g_bandIndex == 0)
        {
            g_bandIndex = 20;
            g_currentFrequency = g_bandList[g_bandIndex].maximumFreq;
            g_bandList[g_bandIndex].currentFreq = g_currentFrequency;
            g_si4735.setFrequency(g_bandList[g_bandIndex].maximumFreq);
            bfoReset();
            return true;
        }
        else
        {
            g_bandIndex--;
            showModulation();
        }
    }

    return false;
}

void loop()
{
    uint8_t x;
    bool skipButtonEvents = false;

    if (g_processFreqChange && millis() - g_lastFreqChange >= 70 && g_encoderCount == 0)
    {
        g_si4735.setFrequency(g_currentFrequency);
        g_processFreqChange = false;
    }
    
    if (millis() - g_lastFreqChange >= 1000)
    {
        showRDS();

        if (g_sMeterOn && !g_settingsActive)
            showSMeter();

        showCharge(false);
    }

    //Encoder rotation check
    if (g_encoderCount != 0)
    {
        if (g_settingsActive)
        {
            if (!g_SettingEditing)
            {
                int8_t prev = g_SettingSelected;
                int8_t next = g_SettingSelected;

                next += g_encoderCount;

                uint8_t pageIdx = g_SettingsPage - 1;
                uint8_t maxOnThisPage = (pageIdx * 6) + 5;
                if (maxOnThisPage >= SettingsIndex::SETTINGS_MAX)
                    maxOnThisPage = SettingsIndex::SETTINGS_MAX - 1;

                if (next < pageIdx * 6)
                    g_SettingSelected = maxOnThisPage;
                else if (next > maxOnThisPage)
                    g_SettingSelected = pageIdx * 6;
                else
                    g_SettingSelected = next;

                DrawSetting(prev, true);
                DrawSetting(g_SettingSelected, true);
            }
            else
            {
                (*g_Settings[g_SettingSelected].manipulateCallback)(g_encoderCount);
                DrawSetting(g_SettingSelected, false);
                delay(MIN_ELAPSED_TIME);
            }
        }
        else if (g_cmdVolume)
            doVolume(g_encoderCount);
        else if (g_cmdStep)
            doStep(g_encoderCount);
        else if (g_cmdBw)
            doBandwidth(g_encoderCount);
        else if (g_cmdBand)
        {
            if (g_encoderCount == 1)
                bandSwitch(true);
            else
                bandSwitch(false);
        }
        else if (isSSB())
        {
            //Special feature to make SSB feel like on expensive TECSUN receivers
            //BFO is now part of main frequency in SSB mode
            const int BFOMax = 16000;
            int step = g_encoderCount == 1 ? getSteps() : getSteps() * -1;
            int newBFO = g_currentBFO + step;
            int redundant = 0;

            if (newBFO > BFOMax)
            {
                redundant = (newBFO / BFOMax) * BFOMax;
                g_currentFrequency += redundant / 1000;
                newBFO -= redundant;
            }
            else if (newBFO < -BFOMax)
            {
                redundant = ((abs(newBFO) / BFOMax) * BFOMax);
                g_currentFrequency -= redundant / 1000;
                newBFO += redundant;
            }

            g_currentBFO = newBFO;

            g_si4735.setSSBBfo(g_currentBFO * -1); //Actually to move frequency forward you need to move BFO backwards
            if (redundant != 0)
            {
                g_si4735.setFrequency(g_currentFrequency);
                g_currentFrequency = g_si4735.getFrequency();
                g_bandList[g_bandIndex].currentFreq = g_currentFrequency;
            }

            g_lastFreqChange = millis();
            g_previousFrequency = 0; //Force EEPROM update
            if (!clampSSBBand()) //If we move outside of current band - switch it
                showFrequency();
            skipButtonEvents = true;
        }
        else
        {
            if (g_encoderCount == 1)
            {
                //g_si4735.frequencyUp();
                g_seekDirection = 1;
            }
            else
            {
                //g_si4735.frequencyDown();
                g_seekDirection = 0;
            }

            //Update frequency
            if (g_currentMode == FM)
            {
                g_currentFrequency += g_tabStepFM[g_FMStepIndex] * g_encoderCount; //g_si4735.getFrequency() is too slow
                if (g_displayRDS)
                    oledPrint(_literal_EmptyLine, 0, 6, DEFAULT_FONT);
            }
            else
                g_currentFrequency += g_tabStep[g_stepIndex] * g_encoderCount;
            uint16_t bMin = g_bandList[g_bandIndex].minimumFreq, bMax = g_bandList[g_bandIndex].maximumFreq;
            if (g_bandList[g_bandIndex].bandType == SW_BAND_TYPE)
            {
                bMin = SW_LIMIT_LOW;
                bMax = SW_LIMIT_HIGH;
            }

            //Special logic for fast and responsive frequency surfing
            if (g_currentFrequency > bMax)
                g_currentFrequency = bMin;
            else if (g_currentFrequency < bMin)
                g_currentFrequency = bMax;

            g_processFreqChange = true;
            g_lastFreqChange = millis();

            showFrequency();
            skipButtonEvents = true;
        }
        g_encoderCount = 0;
        resetEepromDelay();
    }

    if (skipButtonEvents)
        goto saveAttempt;

    //Command-checkers
    if (BUTTONEVENT_SHORTPRESS == btn_Bandwidth.checkEvent(simpleEvent))
    {
        if (!g_settingsActive && g_currentMode != CW)
        {
            g_cmdBw = !g_cmdBw;
            disableCommand(&g_cmdBw, g_cmdBw, showBandwidth);
        }
    }
    if (BUTTONEVENT_SHORTPRESS == btn_BandUp.checkEvent(bandEvent))
    {
        if (!g_settingsActive)
        {
            if (g_sMeterOn || g_displayRDS)
            {
                g_sMeterOn = false;
                g_displayRDS = false;
                updateLowerDisplayLine();
            }
            g_cmdBand = !g_cmdBand;
            disableCommand(&g_cmdBand, g_cmdBand, showModulation);
        }
        else
        {
            switchSettingsPage();
        }
    }
    if (BUTTONEVENT_SHORTPRESS == btn_BandDn.checkEvent(bandEvent))
    {
        if (!g_settingsActive)
        {
            g_cmdVolume = false;
            g_cmdStep = false;
            g_cmdBw = false;
            g_cmdBand = false;
        }
        disableCommand(&g_settingsActive, g_settingsActive, switchSettings);
    }
    if (BUTTONEVENT_SHORTPRESS == btn_VolumeUp.checkEvent(volumeEvent))
    {
        if (!g_settingsActive && g_muteVolume == 0)
        {
            g_cmdVolume = !g_cmdVolume;
            disableCommand(&g_cmdVolume, g_cmdVolume, showVolume);
        }
    }
    if (BUTTONEVENT_SHORTPRESS == btn_VolumeDn.checkEvent(volumeEvent))
    {
        if (!g_cmdVolume)
        {
            uint8_t vol = g_si4735.getCurrentVolume();
            if (vol > 0 && g_muteVolume == 0)
            {
                g_muteVolume = vol;
                g_si4735.setVolume(0);
            }
            else if (g_muteVolume > 0)
            {
                g_si4735.setVolume(g_muteVolume);
                g_muteVolume = 0;
            }
            showVolume();
        }
    }
    if (BUTTONEVENT_SHORTPRESS == btn_Encoder.checkEvent(simpleEvent))
    {
        if (g_cmdBand)
        {
            g_cmdBand = false;
            showModulation();
        }
        else if (g_cmdStep)
        {
            g_cmdStep = false;
            showStep();
        }
        else if (g_cmdBw)
        {
            g_cmdBw = false;
            showBandwidth();
        }
        else if (g_cmdVolume)
        {
            g_cmdVolume = false;
            showVolume();
        }
        else if (g_settingsActive)
        {
            g_SettingEditing = !g_SettingEditing;
            DrawSetting(g_SettingSelected, true);
        }
        else if (g_displayRDS)
            g_rdsSwitchPressed = true;
        else if (isSSB())
        {
            if (!g_settingsActive)
            {
                g_cmdStep = !g_cmdStep;
                disableCommand(&g_cmdStep, g_cmdStep, showStep);
                if (g_sMeterOn || g_displayRDS)
                {
                    g_displayRDS = false;
                    g_sMeterOn = false;
                    updateLowerDisplayLine();
                }
            }
        }
        //Seek in SSB/CW is not allowed
        else if (g_currentMode == FM || g_currentMode == AM)
        {
            if (g_displayRDS)
                oledPrint(_literal_EmptyLine, 0, 6, DEFAULT_FONT);

            if (g_seekDirection)
                g_si4735.frequencyUp();
            else
                g_si4735.frequencyDown();

            g_si4735.seekStationProgress(showFrequencySeek, checkStopSeeking, g_seekDirection);
            delay(30);
            if (g_currentMode == FM)
            {
                uint16_t f = g_si4735.getFrequency() / 10;
                //Interval from 10 to 100 KHz
                g_currentFrequency = f * 10;
                g_si4735.setFrequency(g_currentFrequency);
            }
            else
            {
                g_currentFrequency = g_si4735.getFrequency();
            }
            showFrequency();
        }
    }

    //This is a hack, it allows SHORTPRESS and LONGPRESS events
    //Be processed without complicated overhead
    //It requires to save checkEvent result into a variable
    //That has exact same name as event processing function for this button
    uint8_t agcEvent = btn_AGC.checkEvent(agcEvent);
    if (BUTTONEVENT_SHORTPRESS == agcEvent)
    {
        if (!g_settingsActive || g_settingsActive && !g_displayOn)
        {
            g_displayOn = !g_displayOn;
            if (g_displayOn)
                oled.on();
            else
                oled.off();
        }
    }
    if (BUTTONEVENT_LONGPRESS == agcEvent)
    {
        if (!g_settingsActive)
        {
            if (isSSB())
                doSync(1);
        }
    }
    uint8_t stepEvent = btn_Step.checkEvent(stepEvent);
    if (BUTTONEVENT_SHORTPRESS == stepEvent)
    {
        if (!g_settingsActive)
        {
            g_cmdStep = !g_cmdStep;
            disableCommand(&g_cmdStep, g_cmdStep, showStep);
            if (g_sMeterOn || g_displayRDS)
            {
                g_sMeterOn = false;
                g_displayRDS = false;
                updateLowerDisplayLine();
            }
        }
    }
    if (BUTTONEVENT_LONGPRESSDONE == stepEvent)
    {
        if (!g_settingsActive)
        {
            g_sMeterOn = !g_sMeterOn;
            if (g_sMeterOn)
            {
                g_displayRDS = false;
                showSMeter();
            }
            else
                updateLowerDisplayLine();
        }
    }
    if (BUTTONEVENT_SHORTPRESS == btn_Mode.checkEvent(simpleEvent))
    {
        if (!g_settingsActive)
        {
            //Do nothing on FM mode (unfortunately no NBFM patch), otherwise switch AM modulation
            if (g_currentMode != FM)
            {
                g_prevMode = g_currentMode;
                switch (g_currentMode)
                {
                case AM:
                    //Patch Si473x memory every time when enabling SSB
                    loadSSBPatch();
                    //Allow pass through

                case LSB:
                    g_currentMode++;
                    break;

                case USB:
                    g_currentMode++;
                    g_cmdBw = false;
                    break;

                case CW:
                    g_currentMode = AM;
                    g_ssbLoaded = false;
                    if (g_stepIndex >= g_amTotalSteps)
                        g_stepIndex = 0;

                    g_currentFrequency += (g_currentBFO / 1000);
                    break;
                }

                g_bandList[g_bandIndex].currentFreq = g_currentFrequency;
                g_bandList[g_bandIndex].currentStepIdx = g_stepIndex;
                applyBandConfiguration();
            }
            else
                doRDS();
        }
    }

saveAttempt:
    //Save EEPROM if anough time passed and frequency changed
    if (g_currentFrequency != g_previousFrequency)
    {
        if ((millis() - g_storeTime) > STORE_TIME)
        {
            saveAllReceiverInformation();
            g_storeTime = millis();
            g_previousFrequency = g_currentFrequency;
        }
    }
}

//Overriding original main to save some space
int main(void)
{
    init();
    setup();
    while(1)
        loop();
    return 0;
}