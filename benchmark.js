const { readdirFast, readdirFastSync } = require('./dist');
const fsPromises = require('node:fs/promises');
const fs = require('node:fs');

console.log(JSON.stringify(process.argv));

const searchPath = process.argv[2];

if (!searchPath) {
    console.error("please specify a path to scan eg `npm run benchmark c:\\test` or `node bunchmark c:\\test`");
    return;
}

console.log(`benchmarking scan of ${searchPath}...`);

function benchmark(name, fn) {
    console.time(name);
    const count = fn();
    console.timeEnd(name);
    console.log(`found ${count} files`);
}

benchmark("readdirFastSync", () => {
    let count = 0;

    for (const entry of readdirFastSync(searchPath, true)) {
        if (!entry.isDirectory) {
            count += 1;
        }
    }
    
    return count;
});

benchmark("readdirFastSync manual recursion", () => {
    let count = 0;

    const dirs = [];
    dirs.push(searchPath);

    while (dirs.length > 0) {
        const parentPath = dirs.pop();
        for (const entry of readdirFastSync(parentPath, false)) {
            if (!entry.isDirectory) {
                count += 1;
            } else {
                dirs.push(entry.parentPath + '\\' + entry.name);
            }
        }
    }
    
    return count;
});

benchmark("node readdirSync with stat", () => {
    let count = 0;

    const dirs = [];
    dirs.push(searchPath);

    while (dirs.length > 0) {
        const parentPath = dirs.pop();
        for (const entry of fs.readdirSync(parentPath, { withFileTypes: false })) {
            const fullPath = parentPath + '\\' + entry; // Dirent parentPath not until node 20
            const stats = fs.statSync(fullPath);
            if (stats.isDirectory()) {
                dirs.push(fullPath);
            } else if (stats.isFile()) {
                count += 1;
            }
        }
    }

    return count;
});

// some people say withFileTypes can be faster, let's see...
benchmark("node readdirSync with filetypes and stat", () => {
    let count = 0;

    const dirs = [];
    dirs.push(searchPath);

    while (dirs.length > 0) {
        const parentPath = dirs.pop();
        for (const entry of fs.readdirSync(parentPath, { withFileTypes: true })) {
            const fullPath = parentPath + '\\' + entry.name; // Dirent parentPath not until node 20
            if (entry.isDirectory()) {
                dirs.push(fullPath);
            } else if (entry.isFile()) {
                const stats = fs.statSync(fullPath);
                if (stats.birthtimeMs) {
                    count += 1;
                }
            }
        }
    }

    return count;
});

// presumably it either gets filetypes from the internal libuv call or it calls stat itself? since this
// about 2x to 3x slower than readdirFast, but an order of magnitude faster than the one with stats,
// that must be coming from something else, unless native overhead for a bunch of stat calls is the
// big cause of slowness with readdirSync with stat
benchmark("node readdirSync with filetypes NO stat", () => {
    let count = 0;

    const dirs = [];
    dirs.push(searchPath);

    while (dirs.length > 0) {
        const parentPath = dirs.pop();
        for (const entry of fs.readdirSync(parentPath, { withFileTypes: true })) {
            const fullPath = parentPath + '\\' + entry.name; // Dirent parentPath not until node 20
            if (entry.isDirectory()) {
                dirs.push(fullPath);
            } else if (entry.isFile()) {
                count += 1;
            }
        }
    }

    return count;
});
