#include "Psm.h"
#include "pico/stdlib.h"

Psm* Psm::objList = nullptr;
Psm* Psm::head = nullptr;
Psm* Psm::tail = nullptr;

Psm::Psm()
{
  if (!head) {
    // The first item becomes the head (and tail) of the list
    head = tail = this;
  }
  else {
    // Alter the previous end-of-list item to point at this new item
    tail->next = this;

    // Mark this new item as the new end-of-list
    tail = this;
  }

  // Nothing follows this new item because it is always at end-of-list
  this->next = nullptr;
}

// ----------------------------------------------------------------------------------
// Walk through all the objects that implement Psm functionality and sequentially
// invoke their method associated with the new power state.
void Psm::setState(psmState_t newState)
{
  Psm* instance = head;
  while (instance) {
    switch (newState) {
      case PSM_RUN:
        instance->run();
        break;

      case PSM_SLEEP:
        instance->sleep();
        break;

      case PSM_DEEPSLEEP:
        instance->deepSleep();
        break;

      case PSM_POWEROFF:
        instance->powerOff();
        break;

      default:
        panic("Bad psmState_t");
    }

    instance = instance->next;
  }
}