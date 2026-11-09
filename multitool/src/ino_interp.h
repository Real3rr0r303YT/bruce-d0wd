// Mini-Interpreter fuer eine Teilmenge von Arduino-C++ (.ino)
// Unterstuetzt: setup()/loop(), eigene Funktionen mit int-Parametern und return,
// int/long/byte/bool/... (alles 32-bit Ganzzahl), 1D-Arrays, #define NAME Wert,
// if/else, while, do-while, for, break/continue, alle gaengigen Operatoren,
// Builtins siehe ino_interp.cpp (pinMode, digitalWrite, delay, Serial.print, oled.print ...).
#pragma once
#include <map>
#include <memory>
#include <string>
#include <vector>

class InoHal {
  public:
    virtual ~InoHal() {}
    virtual void serialOut(const std::string &s) = 0;  // Serial.print
    virtual void oledOut(const std::string &s) = 0;    // oled.print
    virtual void oledClear() = 0;
    // Rueckgabe false + err gesetzt -> Laufzeitfehler
    virtual bool pinMode(long pin, long mode, std::string &err) = 0;
    virtual bool digitalWrite(long pin, long val, std::string &err) = 0;
    virtual bool digitalRead(long pin, long &val, std::string &err) = 0;
    virtual bool analogRead(long pin, long &val, std::string &err) = 0;
    virtual bool analogWrite(long pin, long val, std::string &err) = 0;
    virtual bool irSend(const std::string &name, std::string &err) = 0;
    virtual void delayMs(long ms) = 0;  // muss bei Stop-Anforderung frueh zurueckkehren
    virtual void delayUs(long us) = 0;
    virtual long millis() = 0;
    virtual long micros() = 0;
    virtual long random(long lo, long hi) = 0;
    virtual bool stopRequested() = 0;
    virtual void yield() = 0;
    virtual void atLine(int) {}  // aktuelle Quelltextzeile (Diagnose)
};

namespace ino {
struct Node;
using NodeP = std::shared_ptr<Node>;
struct Func {
    std::vector<std::string> params;
    NodeP body;
    bool returnsValue = false;
};
struct InoExec;
struct Var {
    std::vector<long> data;
    bool isArray = false;
};
}  // namespace ino

class InoInterp {
  public:
    // Parst den Quelltext. false -> errorMsg enthaelt "Zeile N: ..."
    bool load(const std::string &src);
    // Fuehrt setup() einmal und loop() endlos aus, bis Stop oder Fehler.
    // Rueckgabe false bei Laufzeitfehler (errorMsg gesetzt).
    bool run(InoHal &hal);
    std::string errorMsg;

  private:
    std::map<std::string, ino::Func> funcs;
    std::vector<ino::NodeP> globalInits;  // globale Variablen-Deklarationen
    // Laufzeit
    InoHal *hal = nullptr;
    std::vector<std::map<std::string, ino::Var>> scopes;  // [0] = global
    std::vector<size_t> frameBase;                        // Beginn des aktuellen Funktions-Frames
    long retVal = 0;
    int depth = 0;
    unsigned long steps = 0;
    bool failed = false;

    friend struct ino::InoExec;
};
