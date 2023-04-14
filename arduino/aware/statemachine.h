#ifndef STATEMACHINE_H
#define STATEMACHINE_H

#define MAX_STATES 3

typedef enum {
  SEND,
  RECEIVED,
//  MOVE_SERVO,
  SLEEP
} StateType;

typedef struct {
  StateType stateType;
  void (*stateLoop)();
  void (*stateTransition)();
} State;

// simple statemachine that registers a looping callback and a transition or initializing callback for a given state
class StateMachine {
  private:
    State states[MAX_STATES];
    uint8_t usedStates;
    State currentState;
    uint32_t stateStart;
        
  public:
    StateMachine() {
      usedStates = 0;
      currentState = {
        SEND, // state type
        nullptr,    // loop callback
        nullptr     // transition callback
      };
      stateStart = millis();
    }

    // add a state to the statemachine
    void addState(StateType stateType, void (*stateLoop)(), void (*stateTransition)()) {
      if(usedStates <= MAX_STATES){
        states[usedStates].stateType = stateType;
        states[usedStates].stateLoop = stateLoop;
        states[usedStates].stateTransition = stateTransition;
        usedStates++;
      }
    }

    // set a new state and peform the transition callback
    void setState(StateType stateType){
      for(uint8_t i = 0; i < usedStates; i++){
        if(states[i].stateType == stateType){
          currentState = states[i];
          stateStart = millis();
          currentState.stateTransition();
          return;
        }
      }
    }

    uint32_t getStateRunTime(){
      return millis() - stateStart;
    }

    StateType getState(){
      return currentState.stateType;
    }

    // perform the current states loop callback
    void update(){
      currentState.stateLoop();
    }
};

#endif
