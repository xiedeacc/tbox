import CryptoJS from 'crypto-js';

export function sha256StringHex(input) {
    const wordArray = CryptoJS.enc.Utf8.parse(input);
    return CryptoJS.SHA256(wordArray).toString(CryptoJS.enc.Hex);
}

export function readChunkAsArrayBuffer(chunk) {
    return new Promise((resolve, reject) => {
        const reader = new FileReader();
        reader.onload = () => resolve(reader.result);
        reader.onerror = reject;
        reader.readAsArrayBuffer(chunk);
    });
}

export function arrayBufferToBase64(buffer) {
    let binary = '';
    const bytes = new Uint8Array(buffer);
    const len = bytes.byteLength;
    for (let i = 0; i < len; i++) {
        binary += String.fromCharCode(bytes[i]);
    }
    return btoa(binary);
}

export function calculateLargeFileHash(file) {
    return new Promise((resolve, reject) => {
        const chunkSize = 32 * 1024 * 1024; // 1MB per chunk
        const reader = new FileReader();
        let offset = 0;
        const sha256 = CryptoJS.algo.SHA256.create();

        reader.onload = function (event) {
            const data = event.target.result;
            const wordArray = CryptoJS.lib.WordArray.create(new Uint8Array(data));
            sha256.update(wordArray);

            offset += data.byteLength;
            if (offset < file.size) {
                readNextChunk();
            } else {
                const hash = sha256.finalize().toString(CryptoJS.enc.Hex);
                resolve(hash);
            }
        };

        reader.onerror = reject;

        function readNextChunk() {
            const slice = file.slice(offset, offset + chunkSize);
            reader.readAsArrayBuffer(slice);
        }

        readNextChunk();
    });
}