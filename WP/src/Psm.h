#if !defined PSM_H
#define PSM_H

// The Power State-Machine class
//
// The class is used to manage transitions between various power states in a system.
// Any object can inherit from this class to receive notifications when the system
// when moves from one state to another so that the object can do object-specific
// things in response to the power state changes.

typedef enum {PSM_RUN, PSM_SLEEP, PSM_DEEPSLEEP, PSM_POWEROFF} psmState_t;

class Psm
{
  public:
    Psm();

    // Object that implement this class are only required to define methods for the
    // states that are meaningful to them.
    virtual void run() {}
    virtual void sleep() {}
    virtual void deepSleep() {}
    virtual void powerOff() {}

    // Set every Psm object in the system to the same power state
    static void setState(psmState_t);

  private:
    // Maintain a single-linked list of all objects that implement a Psm interface
    static Psm* objList;
    static Psm* head;
    static Psm* tail;
    Psm* next;

};

#endif