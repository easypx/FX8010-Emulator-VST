// Copyright 2023 Klangraum
// Engine "fx8010_emulator_v0" (Floating Point version)
// Build: 20230802
// Useful documents:
// https://github.com/kxproject/kX-Audio-driver-Documentation/blob/master/A%20Beginner's%20Guide%20to%20DSP%20Programming.pdf
// https://github.com/kxproject/kX-Audio-driver-Documentation/blob/master/3rd%20Party%20Docs/Other/AS10k1%20Manual%20(2000).pdf
// https://github.com/kxproject/kX-Audio-driver-Documentation/blob/master/3rd%20Party%20Docs/FX8010%20-%20A%20DSP%20Chip%20Architecture%20for%20Audio%20Effects%20(1998).pdf

#ifndef FX8010_H
#define FX8010_H

#include <stdio.h>
#include <vector>
#include <string>
#include <iostream>
#include <chrono>
#include <math.h>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <regex>
#include <map>
#include <array>
#include <unordered_map>

using namespace std;

// Unterdrücke Warnungen:
// 4715 - Nicht alle Codepfade geben einen Wert zurück.
// 4244 - A floating point type was converted to an integer type.
// 4305 - Value is converted to a smaller type in an initialization or as a constructor argument, resulting in a loss of information.
#pragma warning(disable : 4244 4305 4715)

// Defines
#define E 2.71828182845         // Eulersche Zahl
#define PI 3.14159265359        // Kreiszahl Pi
#define SAMPLERATE 48000        // originale Samplerate des DSP
#define AUDIOBLOCKSIZE 32    // Nur zum Testen! Der Block-Loop wird vom VST-Plugin bereitgestellt.
#define DEBUG 0                 // Synaxcheck (Verbose) & Errors, 0 oder 1 = mit/ohne Konsoleausgaben
#define PRINT_REGISTERS 0       // Zeige Registerwerte an. Dauert bei großer AUDIOBLOCKSIZE länger!
#define MAX_IDELAY_SIZE 8192    // max. Gesamtgroesse iTRAM ~170.67 ms (AS10K Manual)
#define MAX_XDELAY_SIZE 1048576 // max. Gesamtgroesse xTRAM ~21,84s (AS10K Manual)

namespace Klangraum
{

    class FX8010
    {
    public:
        FX8010();
        FX8010(int numChannels);
        ~FX8010();

        // Method to initialize lookup tables and other initialization tasks
        void initialize();
        // Der eigentliche Prozess-Loop
        std::vector<float> process(const std::vector<float> &inputSamples);

        // Gibt Anzahl der ausgeführten Instructions zurück
        int getInstructionCounter();
        // Sourcecode laden
        bool loadFile(const string &path);
        struct MyError // Vorwärtsdeklaration notwendig!
        {
            std::string errorDescription = "";
            int errorRow = 1;
        };
        std::vector<FX8010::MyError> getErrorList();
        int setRegisterValue(const std::string &key, float value);
        float getRegisterValue(const std::string &key);
        vector<string> getControlRegisters();
        std::unordered_map<std::string, std::string> getMetaData();
        inline void setChannels(int numChannels_) { numChannels = numChannels_; }
        inline int getChannels() { return numChannels; }
        bool getReadyStatus(){return isReady;}

    private:
        // Enum for FX8010 opcodes
        enum Opcode
        {
            MACS = 0x0,
            MACSN = 0x1,
            MACW = 0x2,
            MACWN = 0x3,
            MACINTS = 0x4,
            MACINTW = 0x5,
            ACC3 = 0x6,
            MACMV = 0x7,
            ANDXOR = 0x8,
            TSTNEG = 0x9,
            LIMIT = 0xa,
            LIMITN = 0xb,
            LOG = 0xc,
            EXP = 0xd,
            INTERP = 0xe,
            SKIP = 0xf,
            IDELAY,
            XDELAY,
            END
        };

        // This Map holds Key/Value pairs to assign instructions(strings) to Opcode(enum/int)
        std::map<std::string, Opcode> opcodeMap = {
            {"macs", MACS},
            {"macsn", MACSN},
            {"macw", MACW},
            {"macwn", MACWN},
            {"macints", MACINTS},
            {"macintw", MACINTW},
            {"acc3", ACC3},
            {"macmv", MACMV},
            {"andxor", ANDXOR},
            {"tstneg", TSTNEG},
            {"limit", LIMIT},
            {"limitn", LIMITN},
            {"log", LOG},
            {"exp", EXP},
            {"interp", INTERP},
            {"skip", SKIP},
            {"idelay", IDELAY},
            {"xdelay", XDELAY},
            {"end", END}};

        // Enum for FX8010 Directives
        // Directives are special instructions which tell the assembler to do certain things at assembly time.
        // Diese Definitionen sind nicht ganz korrekt/vollständig, aber funktionieren im KX-DSP Kontext. (siehe AS10K Manual)
        enum RegisterType
        {
            STATIC = 0,
            TEMP,
            CONTROL,
            INPUT,
            OUTPUT,
            CONST,
            ITRAMSIZE,
            XTRAMSIZE,
            READ,
            WRITE,
            AT,
            CCR,
            // NOISE
        };

        // This Map holds Key/Value pairs to assign registertypes(strings) to RegisterType(enum/int)
        std::map<std::string, RegisterType> typeMap = {
            {"static", STATIC},
            {"temp", TEMP},
            {"control", CONTROL},
            {"input", INPUT},
            {"output", OUTPUT},
            {"const", CONST},
            {"itramsize", ITRAMSIZE},
            {"xtramsize", XTRAMSIZE},
            {"read", READ},
            {"write", WRITE},
            {"at", AT},
            {"ccr", CCR},
            //{"noise", NOISE}
        };

        // FX8010 global storage
        double accumulator = 0; // 63 Bit, 4 Guard Bits, Long type?
        int instructionCounter = 0;
        std::vector<float> outputBuffer;

        // GPR - General Purpose Register
        struct GPR
        {
            int registerType = 0;          // Typ des Registers (z.B. STATIC, TEMP, CONTROL, INPUT_, OUTPUT_)
            std::string registerName = ""; // Name des Registers
            float registerValue = 0;       // Wert des Registers
            int IOIndex = 0;               // 0 - Links, 1 - Rechts
            bool isBorrow = false;         // fuer CCR, Was tut das?
        };

        // Vector, der die GPR enthaelt
        std::vector<GPR> registers;

        // Struct, die eine Instruktion repraesentiert
        struct Instruction
        {
            int opcode = 0;        // Opcode-Nummer
            int operand1 = 0;      // R (Index des GPR in Vektor "registers")
            int operand2 = 0;      // A (Index des GPR in Vektor "registers")
            int operand3 = 0;      // X (Index des GPR in Vektor "registers")
            int operand4 = 0;      // Y (Index des GPR in Vektor "registers")
            bool hasInput = false; // um nicht alle Instructions auf INPUT testen zu muessen
            // hasOutput nicht sinnvoll, Doppelcheck (Instruktion und R)
            bool hasOutput = false; // um nicht alle Instructions auf OUTPUT testen zu muessen
            bool hasNoise = false;
        };

        // Vector, der die Instruktionen enthaelt
        std::vector<Instruction> instructions;

        // Mehrere Lookup Tables in einem Vector, LOG, EXP jeweils mit 31 Exponenten
        vector<vector<double>> lookupTablesLog;
        vector<vector<double>> lookupTablesExp;

        // TRAM Engine
        //----------------------------------------------------------------

        // Angeforderte Delayline Groesse
        int iTRAMSize = 0;
        int xTRAMSize = 0;

        // Create a circular buffer for each delay line. You can use a std::vector to represent the buffer.
        // std::vector<float> smallDelayBuffer; // ATTENTION: Vektoren scheinen trotz reserve() und resize() Probleme mit zufaelligen Werten zu machen!
        // std::vector<float> largeDelayBuffer; //
        float smallDelayBuffer[MAX_IDELAY_SIZE];
        float largeDelayBuffer[MAX_XDELAY_SIZE];

        // Schreib-/Lesepointer
        int smallDelayWritePos = 0;
        int smallDelayReadPos = 0;
        int largeDelayWritePos = 0;
        int largeDelayReadPos = 0;

        // Delayline methods
        inline float readSmallDelay(int position);
        inline float readLargeDelay(int position);
        inline void writeSmallDelay(float sample, int position_);
        inline void writeLargeDelay(float sample, int position_);

        // CCR Register
        inline void setCCR(const float result);
        inline int getCCR();

        // 32Bit Saturation
        inline float saturate(const float input, const float threshold);

        // Lineare Interpolation mit Lookup-Table
        inline double linearInterpolate(double x, const std::vector<double> &lookupTable, double x_min, double x_max);

        // MACINTW
        inline float wrapAround(const float a);

        // ANDXOR Instruction
        inline int32_t logicOps(const GPR &A, const GPR &X_, const GPR &Y_);

        // Debug Registers
        void printRegisters(const int instruction, const float value1, const float value2, const float value3, const float value4, const double accumulator);

        // Syntax Check
        bool syntaxCheck(const std::string &input);

        // GPR-Index zurückgeben
        int findRegisterIndexByName(const std::vector<GPR> &registers, const std::string &name);

        // Map registernames from sourcecode instruction to register indexes
        int mapRegisterToIndex(const string &registerName);

        // Definiere die Fehlercodes
        enum ErrorCode
        {
            ERROR_NONE = 0,
            ERROR_INVALID_INPUT,
            ERROR_DIVISION_BY_ZERO,
            ERROR_MULTIPLE_VAR_DECLARE,
            ERROR_VAR_NOT_DECLARED,
            ERROR_INPUT_FOR_R_NOT_ALLOWED,
            ERROR_NO_END_FOUND,
            ERROR_IO_INDEX_OUT_OF_RANGE,
            ERROR_SYNTAX_NOT_VALID,
            ERROR_ITRAMSIZE_TO_LARGE,
            ERROR_XTRAMSIZE_TO_LARGE
            // Weitere Fehlercodes hier...
        };

        std::map<int, std::string> errorMap;
        MyError error;
        vector<MyError> errorList;
        int errorCounter = 1;

        std::vector<double> createLogLookupTable(double x_min, double x_max, int numEntries, int exponent);
        std::vector<double> createExpLookupTable(double x_min, double x_max, int numEntries, int exponent);
        std::vector<double> mirrorYVector(const std::vector<double> &inputVector);
        std::vector<double> concatenateVectors(const std::vector<double> &vector1, const std::vector<double> &vector2);
        std::vector<double> negateVector(const std::vector<double> &inputVector);

        int numChannels;

        vector<string> controlRegisters;

        // Fast White Noise
        // Linear Feedback Shift Register (LFSR) als Pseudo-Zufallszahlengenerator
        // https://www.musicdsp.org/en/latest/Synthesis/216-fast-whitenoise-generator.html
        float g_fScale = 2.0f / 0xffffffff;
        // Seeds
        int32_t g_x1 = 0x70f4f854;
        int32_t g_x2 = 0xe1e9f0a7;
        float whitenoise();

        // Map mit Metadaten
        std::unordered_map<std::string, std::string> metaMap;

        // Konvertierung Float <-> 32Bit Integer
        float intToFloat(int32_t intValue);
        int32_t floatToInt(float floatValue);

        bool isReady = false;
    };

} // namespace Klangraum

#endif // FX8010_H
