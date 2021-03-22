# Demo applications and references

[![Forum](https://img.shields.io/discourse/users.svg?server=https%3A%2F%2Fforum.uavcan.org&color=1700b3)](https://forum.uavcan.org)

A weakly organized collection of usage demos and examples that can be used to bootstrap product development.


## Background

Adopting UAVCAN may seem like a paradigm shift for an engineer experienced with prior-art technologies
due to its hard focus on service-orientation and zero-cost abstraction.
In order to make sense of the materials presented here,
**you should first read [the UAVCAN Guide](https://uavcan.org/guide)**
as well as the brief architecture overview provided for the
[DS-015 UAVCAN Drone Standard](https://github.com/UAVCAN/public_regulated_data_types/tree/master/reg/drone).

UAVCAN heavily delegates responsibility to individual participants of the
distributed computing system instead of aggregating it in a central agent responsible for everything.
Each node is responsible for
[abstracting away the specifics of its job from its peers](https://en.wikipedia.org/wiki/Service-oriented_architecture).
This approach is motivated by the growing complexity of intra-vehicular computing and control systems.


## How to use this repository

There is a separate directory per demo.
Demos may depend on the components published by the UAVCAN Consortium, such as
[Libcanard](https://github.com/UAVCAN/libcanard) or the
[public regulated DSDL definitions](https://github.com/UAVCAN/public_regulated_data_types/).
These are collected under `submodules/`.
You will need to add them to your application separately in whatever way suits your workflow best ---
as a Git submodule, by copy-pasting the sources, using CMake's `ExternalProject_Add()`, etc.

While the code is permissively licensed to facilitate its integration into closed-source products,
this repository is not accessible to non-members of the UAVCAN Consortium.
