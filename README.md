# Multiplayer, network Bomberman

This repository contains an implementation of the Bomberman game which I have developed as part of my Computer Networks course at the University of Warsaw (MIMUW). It uses Boost::ASIO for the networking part and Boost::PFR for reflecion, which I have used for an elegant serialization implementation.

Please take a look at the below code for serialization:
https://github.com/eerio/network-bomberman/blob/0de64c66894b4debea75a38fc277cb82e14ed358/serialization.hpp#L48-L64
It relies on reflection heavily: notably, it uses the `boost::pfr::for_each_field`, which is easy to do in Python, but certainly not in C++ :) (but I heard that there is a plan to add reflection to the C++ standard). Using the functions from this file (`serialization.hpp`), you can serialize `std::vector`, `std::map`, `std::string`, `std::variant` and simple custom `struct`s using just the good old `<<` operator (e.g. `buffer << struct_representing_my_message`).

The repository doesn't contain a GUI - we have been provided an implementation of it, authored by the problem's creator. The main files are `robots-client.cpp` and `robots-server.cpp`.
