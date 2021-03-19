# Demo applications and references

[![Forum](https://img.shields.io/discourse/users.svg?server=https%3A%2F%2Fforum.uavcan.org&color=1700b3)](https://forum.uavcan.org)

This repository is a weakly organized collection of usage demos and examples that can be used to bootstrap
product development.
While the code is permissively licensed to facilitate its integration into closed-source products,
at this moment this repository is not accessible to non-members of the UAVCAN Consortium.

## Directory layout

There is a separate directory per demo.
Each demo may depend on the components published by the UAVCAN Consortium, such as
[Libcanard](https://github.com/UAVCAN/libcanard) or the
[public regulated DSDL definitions](https://github.com/UAVCAN/public_regulated_data_types/).
These are collected under `submodules/`.
You will need to add them to your application separately in whatever way suits your workflow best ---
as a Git submodule, by copy-pasting the sources, using CMake's `ExternalProject_Add()`, etc.
