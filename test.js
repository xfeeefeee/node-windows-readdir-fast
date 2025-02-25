const { readdirFast, readdirFastSync } = require('./dist');

const allFiles = [];
const beforeSync = Date.now();
let countSync = 0;
for (const entry of readdirFastSync('C:\\temp', true)) {
	//console.log(entry);
	countSync += 1;
	//allFiles.push(entry);
}
allFiles.sort((a, b) => b.ctimeMs - a.ctimeMs);
const afterSync = Date.now();
console.log(`await ${afterSync - beforeSync}ms`);
console.log(countSync);


async function awaitTest() {
	const beforeAwait = Date.now();
	let countAwait = 0;
	for (const entry of await readdirFast('C:\\temp', false)) {
		//console.log(entry);
		countAwait += 1;
	}
	const afterAwait = Date.now();
	console.log(`await ${afterAwait - beforeAwait}ms`);
	console.log(countAwait);
}


awaitTest()


