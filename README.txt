This is a program to convert large Microsoft Virtual PC and/or Hyper-V disk
images to block devices or raw (sparse) image files. qemu-img has functionality
to do this conversion, but does not support an updated version of the format
that supports very large disk images.

Usage is straightforward:

./devhd <in> <out>

This will copy all sparse chunks from the <in> file to the <out> file at the
appropriate offset. It is assumed that the <out> file is of sufficient size to
hold all chunks in the input, and that all parts of the output which are not
present in the input are zero (sparse files and zeroed block devices of the
appropriate size satisfy this criteria).

This only works on sparse images. For differencing disks, you're on your own,
and for raw images dd(1) should be able to copy out the bytes you're after
without any additional processing.
