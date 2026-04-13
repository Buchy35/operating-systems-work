# MTS Multi Thread Scheduling
# Cole Buchinski
# 2025.10.31

## Overview
Implements the railway scheduling system from CSC360 p2 spec.

Enforces the following rules:

1. Only one train may be on the track at a time
2. Only loaded trains can cross
3. High priority trains are dispatched first
4. If two trains have the same priority
    - if travelling in the same direction, the first one to finsih loading goes first
    - if travelling in opposite directions, the one going in the opposite direction of the previous train goes first
5. To avoid starvation, if two trains going in the same direction have crossed consecutively, the next train must be going the opposite direction

The simulation prints to both stdout and output.txt

## Compilation
Run 'make' to compile
creates the executable 'mts'
to clean up artifacts, use make clean
to run use make run

## Usage
Run the simulation with one input file:
./mts input.txt

## Estimated Execution time of test file
4.1 seconds

