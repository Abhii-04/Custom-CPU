To compile the whole code of main run:

` clang++ Main.cpp -o final_executable $(llvm-config --cxxflags --ldflags --system-libs --libs all)`

Then run 
`./final_executable`

This will probably only run on linux

