# node-windows-readdir-fast

fast readdir with filetimes lengths and attributes for Windows

## why

while readdir in node.js is fast, it does not return file lengths or file times. this requires a separate stat call, which can be slow.
this library uses NtQueryDirectoryFileEx (similar to FindFirstFileEx) for performance, which already includes file length and file times.

for my personal uses, this can scan a path with around 100,000 files and hundreds of directories in about 280ms as compared to over 13000ms
(13 seconds!) using node's readdir and stat, which is ~50x faster. actual performance will vary, but generally will always be much faster.

if you just need the names of the entries in the path, readdir works fine. if you need file lengths or file times, this is faster.

## installation

```bash
npm install node-windows-readdir-fast
```

then rebuild the native module
```bash
npm rebuild
```

note that electron is special and may need to be built via [electron-rebuild](https://github.com/electron/rebuild)

there are no prebuilt binaries currently

## native module resolution

if you have issues with the native module loading, make sure `npm rebuild` or `electron-rebuild` actually completed. normally the module
should be in `./node_modules/node-windows-readdir-fast/build/Release/node-windows-readdir-fast.node` and the `bindings` library (that helps
find and load the native module) uses the location of the file requiring the _native_ module to search for the file. this means that if you
are using a bundler (eg webpack etc) you may need to specify this package as an external to exclude it from the bundle; otherwise the file
internally requiring the native module will be within your bundle and not `./node_modules/node-windows-readdir-fast/dist/index.mjs` etc.

alternatively you can modify the `NODE_PATH` environment variable if needed, or add a custom build step to copy `node-windows-readdir-fast.node`
somewhere else. the `bindings` library should give a helpful message showing the locations it searched. for example, you could have a step to
copy the file to a `./build` directory.

## usage

```js
const { readdirFast } = require('node-windows-readdir-fast')
const { readdirFast, readdirFastSync, FileAttribute } = require('node-windows-readdir-fast')
```

or

```js
import { readdirFast } from 'node-windows-readdir-fast'
import { readdirFast, readdirFastSync, FileAttribute } from 'node-windows-readdir-fast'
```

`readdirFast(dir: string, recurse: boolean)` returns a Promise that can be awaited to return a generator that yields `DirentStats` objects,
while `readdirFastSync(dir: string, recurse: boolean)` directly returns a generator that yields `DirentStats` objects.

set `recurse` to true to automatically recurse into subdirectories.

the `dir` must be an absolute path. however, you can freely mix both backslashes and forward slashes, though the `parentPath` will always return
windows style backslashes.

simple usage
```js
for (const entry of await readdirFastSync('C:\\cats', true)) { console.log(entry) }
for (const entry of readdirFastSync('C:\\cats', true)) { console.log(entry) }
```

examples using ES2025 iterator helpers
```js
const fileNames = (await readdirFastSync('C:\\cats', true)).filter(e => !e.isDirectory).map(e => e.name).toArray()
const fileNames = readdirFastSync('C:\\cats', true).filter(e => !e.isDirectory).map(e => e.name).toArray()
``` 

the `DirentStats` object. file times are in milliseconds for easy conversion to `Date` objects

```ts
type DirentStats = {
	name: string;
	parentPath: string; /** will not end in a separator, \\\\?\\ prefix will be removed */
	isDirectory: boolean; /** derived from attributes for ease of use */
	attributes: number; /** bitmask of FileAttributes enum */
	length: number;
	birthtimeMs: number;
	mtimeMs: number;
}
```

You can easily generate the full path by adding the separator

```js
const fullPath = `${e.parentPath}\\${e.name}`;
```

attributes corresponds to the win32 FILE_ATTRIBUTE_NORMAL etc constants

```js
const FileAttribute = {
	ReadOnly:	1,
	Hidden:		2,
	System:		4,
	Directory:	16,
	Archive:	32,
	Normal:		128, /** only valid when used alone, don't ask me */
	Temporary:	256,
	Compressed:	2048,
};
```

if the entry is a directory, the `isDirectory` member of `DirentStats` will be set for convenience,
so usually you will not need to use the FileAttribute enum or attributes member directly

the async version will wait until the entire scan is complete before resolving, rather than stream results as we get them.
In your own code, you could just scan the directories without `recurse` and individually await the result of each directory
or even run multiple subdirectory scans in parallel, though each directory itself still will not resolve until the entire
directory is scanned. this can still be useful for situations with lots of subdirectories as compared to one directory 
with a lot of files.

if the `dir` is inaccessible or fails to open for any reason, an exception will be thrown or the promise will be rejected.
however, if `recurse`, subdirectories that fail to be accessed will be silently ignored.

## benchmarks

far from a scientific test, i included a simple `benchmark.js` file that you can use to compare. you can run this via
`npm run benchmark c:\path` or `node benchmark c:\path`

you will want to run this multiple times due to filesystem metadata caching within windows. additionally, this is intended
to benchmark longer operations, not smaller operations, so you should create your own with multiple runs and warmup etc
to get accurate benchmarks if needed for your use case.

example:

```
benchmarking scan of c:\scanTest...
readdirFastSync: 280.043ms
found 91930 files
readdirFastSync manual recursion: 285.067ms
found 91930 files
node readdirSync with stat: 13.980s
found 91930 files
node readdirSync with filetypes and stat: 13.256s
found 91930 files
node readdirSync with filetypes NO stat: 307.637ms (note this does not include the filetimes or length)
found 91930 files
```

## implementation notes

this supports long path names (over 250 chars etc). internally, the \\\\?\\ prefix will be added automatically if needed, but
the `parentPath` will not include it. You can pass a `dir` that begins with \\\\?\\ yourself as well but this is not necessary.

internally, this uses NtQueryDirectoryFileEx for performance, and always returns file times and length.

because creating objects within a native node.js addon is slower than you might think, internally this generates a buffer that is
then parsed in javascript with a generator function. this is many times faster than the overhead of creating objects via Node-API

currently this does not include the atime / LastAccessTime in the results, mostly because I've never actually seen that be useful.

could have been written with the classic node callback style, but decided to just use Promises directly.

this was created to fulfill a specific need after being surprised that such a solution did not already exist. please feel free to
contribute or fork or adapt to your own projects. there are certainly edge cases that i have not tested or even considered.
however, it is stable and working great for the scenario i created it for, simply scanning local disks.

potential issues that probably work okay but have not been tested: junctions, hard and soft symlinks, network paths, etc

## building

`npm rebuild` to build the native addon. then `npm run build` to generate the modules and typescript definitions in `dist/`

you will need the usual build utils for windows of course

## tests

`node test` to run basic tests. unfortunately i am unable to make my main tests file public for a variety of reasons, but there is
a simple `test.js` that can be expanded upon in the future.

## todo

- meow

## license

xfeeefeee disclaims copyright to this source code. In place of a legal notice, here is a blessing:

```
May you do good and not evil.
May you find forgiveness for yourself and forgive others.
May you share freely, never taking more than you give.
```
