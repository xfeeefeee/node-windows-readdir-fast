const bindings = require("bindings")("node-windows-readdir-fast.node");

/** corresponds to the win32 FILE_ATTRIBUTE_NORMAL etc constants */
export const FileAttribute = Object.freeze({
	ReadOnly:	1,
	Hidden:		2,
	System:		4,
	Directory:	16,
	Archive:	32,
	Normal:		128, /** only valid when used alone, don't ask me */
	Temporary:	256,
	Compressed:	2048,
});

/** directory entry similar to Dirent, with file length, creation time, and modified time included */
export type DirentStats = {
	name: string;
	parentPath: string; /** will not end in a separator, \\\\?\\ prefix will be removed */
	isDirectory: boolean; /** derived from attributes for ease of use */
	attributes: number; /** bitmask of FileAttributes enum */
	length: number;
	birthtimeMs: number;
	mtimeMs: number;
}

/** yield a DirentStats for each entry in the internal ArrayBuffer from the native doFastReadDir call */
function* parseBuffer(buf: ArrayBuffer): Generator<DirentStats, void, unknown> {
	const textDecoder = new TextDecoder('utf-16');
	const view = new DataView(buf);
	let pos = 0;
	
	let parentPath = '';
	while (pos < buf.byteLength) {
		const nameBytes = view.getInt32(pos, true);
		pos += 4;
		const name = textDecoder.decode(new DataView(buf, pos, nameBytes));
		pos += nameBytes;
		const attributes = view.getUint32(pos, true);
		pos += 4;
		
		// special record for each directory entry so the full path does not need repeated for every file
		if (attributes === 0xFFFFFFFF) {
			parentPath = name;
			continue;
		}
		
		const isDirectory = (attributes & 16) ? true : false;
		
		const length = view.getFloat64(pos, true);
		pos += 8;
		const birthtimeMs = view.getFloat64(pos, true);
		pos += 8;
		const mtimeMs = view.getFloat64(pos, true);
		pos += 8;
		
		yield { name, parentPath, isDirectory, attributes, length, birthtimeMs, mtimeMs };
	}
}

/**
 * read a directory and return all entries
 * 
 * @param dir - must be an absolute path
 * @param recurse - whether to recursively read subdirectories
 * 
 * @remarks long paths are supported automatically and internally converted to \\\\?\\ syntax as needed
 * 
 * this will wait until the entire scan is complete before resolving, rather than stream results as we get them.
 * 
 * @returns a promise that resolves to a generator that yields DirentStats objects
 * 
 * @example
 * basic usage
 * ```
 * for (const entry of await readdirFastSync('C:\\cats', true)) { console.log(entry) }
 * ```
 * 
 * using ES2025 iterator helpers
 * ```
 * const fileNames = (await readdirFastSync('C:\\cats', true)).filter(e => !e.isDirectory).map(e => e.name).toArray()
 * ``` 
 */
export async function readdirFast(dir: string, recurse: boolean) {
	return parseBuffer(await bindings.doFastReadDir(dir, recurse));
}

/**
 * read a directory and return all entries
 * 
 * @param dir - must be an absolute path
 * @param recurse - whether to recursively read subdirectories
 * 
 * @remarks long paths are supported automatically and internally converted to \\\\?\\ syntax as needed
 * 
 * @returns a generator that yields DirentStats objects
 * 
 * @example
 * basic usage
 * ```
 * for (const entry of readdirFastSync('C:\\cats', true)) { console.log(entry) }
 * ```
 * 
 * using ES2025 iterator helpers
 * ```
 * const fileNames = readdirFastSync('C:\\cats', true).filter(e => !e.isDirectory).map(e => e.name).toArray()
 * ``` 
 */
export function readdirFastSync(dir: string, recurse: boolean) {
	return parseBuffer(bindings.doFastReadDirSync(dir, recurse));
}
