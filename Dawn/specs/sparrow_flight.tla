----------------------------- MODULE sparrow_flight -----------------------------
EXTENDS Naturals, TLC

CONSTANT Sparrows

Grounded == "grounded"
Hydroplaning == "hydroplaning"

VARIABLE sparrowState

Init == sparrowState = [sparrow \in Sparrows |-> Grounded]

Drive(sparrow) ==
    /\ sparrow \in Sparrows
    /\ sparrowState' = [sparrowState EXCEPT ![sparrow] = Hydroplaning]

Stop(sparrow) ==
    /\ sparrow \in Sparrows
    /\ sparrowState' = [sparrowState EXCEPT ![sparrow] = Grounded]

Next == \E sparrow \in Sparrows : Drive(sparrow) \/ Stop(sparrow)

TypeInvariant == \A sparrow \in Sparrows : sparrowState[sparrow] \in {Grounded, Hydroplaning}
SparrowsCannotFly == \A sparrow \in Sparrows : sparrowState[sparrow] # "flying"

Spec == Init /\ [][Next]_sparrowState

THEOREM SparrowsCannotFlyForever == Spec => []SparrowsCannotFly
=============================================================================
