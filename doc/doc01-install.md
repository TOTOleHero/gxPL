Basic Installation

These are installation instructions.

Clone sources from epsilonrt.com git repos (or untar archive file):

<div class="fragment"><pre class="fragment">
git clone http://git.epsilonrt.com/gxPL
</pre></div>

Build and install the library:

<div class="fragment"><pre class="fragment">
cd gxPL
make
sudo make install
</pre></div>

Build and install the tools (hub, logger ...):

<div class="fragment"><pre class="fragment">
cd tools
make
sudo make install
</pre></div>

Run hub tool as daemon:

<div class="fragment"><pre class="fragment">
gxpl-hub
</pre></div>

Build and run clock example:

<div class="fragment"><pre class="fragment">
cd ../examples
make
cd clock
./gxpl-clock -xlpdebug
</pre></div>

The shared and static libraries are installed into /usr/local/lib and 
header files is installed in /usr/local/include.  
You can changed the destinations in the Makefile if you'd like.