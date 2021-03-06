# dynStruct
dynStruct is a tool using dynamoRio to monitor memory accesses of an ELF binary via a data gatherer,
and use this data to recover structures of the original code.

The data gathered can also be used to quickly find where and by which function a member of a structure is write or read.

## Requirements
* CMake >= 2.8
* [DynamoRIO](https://github.com/DynamoRIO/dynamorio)
* Python3

## Setup
Set the environment variable DYNAMORIO_HOME to the absolute path of your
DynamoRIO installation

Execute `build.sh`

To compile dynStruct for a 32bits target on a 64bits os execute `build.sh 32`

Install dependencies for dynStruct.py: `pip3 install -r requirements.txt`

## Data gatherer

###Usage

```
drrun -opt_cleancall 3 -c <dynStruct_path> <dynStruct_args> -- <prog_path> <prog_args>

  -h print this help
  -o <file_name>	set output name for json file
			 if a file with this name already exist the default name will be used
			 in the case of forks, default name will be used for forks json files
			 (default: <prog_name>.<pid>)
  -d <dir_name>		set output directory for json files
		         (default: current directory)
  - 			print output on console
			 Usable only on very small programs
  -w <module_name>	wrap <module_name>
			 dynStruct record memory blocks only
			 if *alloc is called from this module
  -m <module_name>	monitor <module_name>
			 dynStruct record memory access only if
			 they are done by a monitore module
  -a <module_name>	is used to tell dynStruct which module implements
			 allocs functions (malloc, calloc, realloc and free)
			 this has to be used with the -w option (ex : "-a ld -w ld")
			 this option can only be used one time
for -w, -a and -m options modules names are matched like <module_name>*
this allow to don't care about the version of a library
-m libc.so match with all libc verison

The main module is always monitored and wrapped
Tha libc allocs functions are always used (regardless the use of the -a option)

Example : drrun -opt_cleancall 3 -c dynStruct -m libc.so - -- ls -l

This command run "ls -l" and will only look at block allocated by the program
but will monitor and record memory access from the program and the libc
and print the result on the console
```

### Example

We are going to analyse this little program.

```C
void print(char *str)
{
  puts(str);
  str[1] = 'a';
  puts(str);
}

int main()
{
  char *str;

  str = malloc(5);
  strcpy(str, "test");
  str[4] = 0;
  print(str);

  free(str);
}
```
Which after compilation look like this
![Example disassembly](http://i.imgur.com/L2i4zJS.png)

If we run `drrun -c  dynStruct - -- tests/example` we get
```
test
tast
block : 0x0000000000603010-0x0000000000603015(0x5) was free
alloc by 0x0000000000400617(main : 0x00000000004005f9 in example) and free by 0x000000000040064c(main : 0x00000000004005f9 in example)
	 WRITE :
	 was access at offset 1 (1 times)
	details :
			 1 bytes were accessed by 0x00000000004005e7 (print : 0x00000000004005b6 in example) 1 times
	 was access at offset 4 (2 times)
	details :
			 1 bytes were accessed by 0x000000000040062a (main : 0x00000000004005f9 in example) 1 times
			 1 bytes were accessed by 0x0000000000400636 (main : 0x00000000004005f9 in example) 1 times
	 was access at offset 0 (1 times)
	details :
			 4 bytes were accessed by 0x0000000000400624 (main : 0x00000000004005f9 in example) 1 times
```
We see all the write accesses on str done by the program himself.
We can notice the 4 bytes access at offset 0 of the block due to gcc optimisation for initializing the string.

Now if we run `drrun -c  dynStruct -m libc - -- tests/example` we are going to monitor all the libc accesses, and we get
```
test
tast
block : 0x0000000000603010-0x0000000000603015(0x5) was free
alloc by 0x0000000000400617(main : 0x00000000004005f9 in example) and free by 0x000000000040064c(main : 0x00000000004005f9 in example)
	 READ :
	 was access at offset 1 (2 times)
	details :
			 1 bytes were accessed by 0x00007fca292156c2 (_IO_default_xsputn : 0x00007fca29215650 in libc.so.6) 1 times
			 1 bytes were accessed by 0x00007fca29213c9d (_IO_file_xsputn@@GLIBC_2.2.5 : 0x00007fca29213b70 in libc.so.6) 1 times
	 was access at offset 2 (2 times)
	details :
			 1 bytes were accessed by 0x00007fca292156c2 (_IO_default_xsputn : 0x00007fca29215650 in libc.so.6) 1 times
			 1 bytes were accessed by 0x00007fca29213c9d (_IO_file_xsputn@@GLIBC_2.2.5 : 0x00007fca29213b70 in libc.so.6) 1 times
	 was access at offset 3 (2 times)
	details :
			 1 bytes were accessed by 0x00007fca292156c2 (_IO_default_xsputn : 0x00007fca29215650 in libc.so.6) 1 times
			 1 bytes were accessed by 0x00007fca29213c81 (_IO_file_xsputn@@GLIBC_2.2.5 : 0x00007fca29213b70 in libc.so.6) 1 times
	 was access at offset 0 (5 times)
	details :
			 1 bytes were accessed by 0x00007fca292156c2 (_IO_default_xsputn : 0x00007fca29215650 in libc.so.6) 1 times
			 16 bytes were accessed by 0x00007fca292210ca (strlen : 0x00007fca292210a0 in libc.so.6) 2 times
			 4 bytes were accessed by 0x00007fca29224c2e (__mempcpy_sse2 : 0x00007fca29224c00 in libc.so.6) 1 times
			 1 bytes were accessed by 0x00007fca29213c9d (_IO_file_xsputn@@GLIBC_2.2.5 : 0x00007fca29213b70 in libc.so.6) 1 times
	 WRITE :
	 was access at offset 1 (1 times)
	details :
			 1 bytes were accessed by 0x00000000004005e7 (print : 0x00000000004005b6 in example) 1 times
	 was access at offset 4 (2 times)
	details :
			 1 bytes were accessed by 0x000000000040062a (main : 0x00000000004005f9 in example) 1 times
			 1 bytes were accessed by 0x0000000000400636 (main : 0x00000000004005f9 in example) 1 times
	 was access at offset 0 (1 times)
	details :
			 4 bytes were accessed by 0x0000000000400624 (main : 0x00000000004005f9 in example) 1 times

```
Now all the read accesses done by the libc are listed.

### Forking (or execve) programs
If you use the data gatherer on a forking program, a file per process will be written.
This allow to analyse each process independently.
In the case of an execution of an other program the new file will have the name of the program executed.
If you don't want the data gatherer to follow new processes use the options -no_follow_children for drrun (example : `drrun -no_follow_children -opt_cleancall 3 -c dynStruct -m libc.so - -- ls -l`)

### Known issue
The data gatherer write the output file only at the end of the execution and actually store all data in memory during execution.
The result is for complexe program (like emacs) will used a huge amount of mememory (more than 4go) to run.
Some optimisations about this memory overhead will come soon.

## Structure recovery

The python script dynStruct.py do the structure recovery and can start the web_ui.

The idea behind the structure recovery is to have a quick idea of the structures used by the program.

It's impossible to recover exactly the structures used in the original source code, so some choices had to be made.
To recover the size of members dynStruct.py look at the size of the accesses for a particular offset, it keep the more used
size, if 2 or more size are used the same number of time it keep the smaller size.

All type are ```uint<size>_t```, all the name are ```offset_<offset_int_the_struct>```.
Some offset in blocks have no read or write accesses in the ouput of the dynStruct dynamoRIO client, so the empty offset are fill
with array called ```pad_offset<offset_in_the_struct>```, all pading are uint8_t.
Array are detected, 5 or more consecutive members of a struct with the same size is considered as an array.
Array are named ```array_<offset_int_the_struct>```.
The last thing that is detected is array of structure, named ```struct_array_<offset_in_the_struct>```

The recovery of struct try to be the most compact as possible, the output will look like :
```
struct struct_14{
	uint32_t array_0x0[5];
	struct {
		uint64_t offset_0x0;
		uint8_t offset_0x8;
		uint8_t pad_offset_0x9[7];
	}struct_array_0x14[2];
} 
```
The recovery process can take several minutes if there are large block.
### Usage
```
usage: dynStruct.py [-h] [-d DYNAMO_FILE] [-p PREVIOUS_FILE] [-o OUT_PICKLE]
                    [-n] [-e <file_name>] [-c] [-w] [-l BIND_ADDR]

Dynstruct analize tool

optional arguments:
  -h, --help        show this help message and exit
  -d DYNAMO_FILE    output file from dynStruct dynamoRio client
  -p PREVIOUS_FILE  file to load serialized data
  -o OUT_PICKLE     file to store serialized data.
  -n                just load json without recovering structures
  -e <file_name>    export structures in C style on <file_name>
  -c                print structures in C style on console
  -w                start the web view
  -l BIND_ADDR      bind addr for the web view default 127.0.0.1:24242
```
### Example
```
gcc tests/test.c -o tests/test
drrun -opt_cleancall 3 -c dynStruct -o out_test -- tests/test
python3 dynStruct.py -d out_test  -c
```
will display
```
//total size : 0x20
struct struct_2 {
	uint32_t offset_0x0;
	uint32_t offset_0x4;
	uint32_t offset_0x8;
	uint8_t pad_offset_0xc[4];
	uint64_t offset_0x10;
	uint8_t offset_0x18;
	uint8_t pad_offset_0x19[7];
};

//total size : 0x18
struct struct_3 {
	uint64_t offset_0x0;
	uint64_t offset_0x8;
	uint64_t offset_0x10;
};
```

The same output can be obtained via a serialized file:
```
python3 dynStruct.py -d out_test  -o serialize_test
python3 dynStruct.py -p serialize_test -c
```

Serialized file allow you to load data from a binary without having to redo the structure recovery. Serialized file also saved modification done to structures via the web ui.

### Recovery accuracy
The recovering of structure is actually not always accurate but it can still give you a good idea of the internal structures of a program. Improving this will be the main goal in the following month. 

## Web interface
DynStruct has a web interface which display raw data from the gatherer and the structures recovered by dynStruct.py

This web interface can be start by using dynStruct.py with the -w option (and -l to change the listening ip/port of the interface).

If we took the last previous example we can start the web interface with:
```
python3 dynStruct.py -p serialize_test -w
```
When you go to the web interface you will see something like:
![Block search view](http://i.imgur.com/YdJqAQx.png)

### Navigation
The navbar on top allow you to switch between the different data dynStruct can display.  
Blocks and accesses are raw data from data gatherer (but more readable than console ouput of the gatherer and with search fields).  
Structures contain structures recovered by dynStruct.py and the structures created by the user if any.  
Download header allow you to download a C style header with the actuals structure (similar to ./dynStruct.py -e or ./dynStruct.py -c).

### Blocks and Access
The detailed view of blocks display blocks informations, all accesses made in this block and a link to the corresponding structure if any.
![Block detailed view] (http://i.imgur.com/sNhQBMQ.png)

### Structures
You can access structures detailed view by clicking on the name of the structure in the structures search page.
![structure detailed view] (http://i.imgur.com/qXaESky.png)

On this view you have the information about the structure, the members of the structure and the list of blocks which are instances of this structure. You can edit the members of the structure and the list of blocks associated with this structure.    
  
There is also a detailed view for each member with the list of accesses made to this member on each block associated with the structure.
![member detailed view] (http://i.imgur.com/G5Y2DgQ.png)

A member can be an inner structure, an array, an array of structure or a simple member (everything which don't match with the previous categories). Union and bitfield are not actually handle by dynStruct.

When you edit a structure you can remove it, remove a member (on the edit member view), or edit a member (it's name, type, size and number of units in the case of an array). You can also create new member in the place of any padding member.
![edit member view](http://i.imgur.com/jgZBMzm.png)

On the Edit instance view you can select multiple blocks (by clicking on them) of the first list to remove them and multiple blocks on the second list to add them. When you click on Edit instances both selected suppression and addition will be executed.  
On the second list only blocks with the same size than the structure and not already associated with the structure will be displayed.
![edit instance view] (http://i.imgur.com/an97OHp.png)

### Edits saving
All edits are saved in the serialized file if any (work with files give with args -o and -p).
