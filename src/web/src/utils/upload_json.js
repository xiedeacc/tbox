import { readChunkAsArrayBuffer, arrayBufferToBase64, calculateLargeFileHash } from './utils.js';

export async function uploadFileInChunks(file, onProgress, onSpeedUpdate) {
    const chunkSize = 16 * 1024 * 1024; // 1MB
    const totalChunks = Math.ceil(file.size / chunkSize);
    // const fileHash = await calculateLargeFileHash(file);
    const fileHash = '4084ec48f2affbd501c02a942b674abcfdbbc6475070049de2c89fb6aa25a3f0'

    for (let i = 0; i < totalChunks; i++) {
        const start = i * chunkSize;
        const end = Math.min(start + chunkSize, file.size);
        const chunk = file.slice(start, end);

        const chunkData = await readChunkAsArrayBuffer(chunk);
        const base64Chunk = arrayBufferToBase64(chunkData);
        const jsonPayload = JSON.stringify({
            path: file.name,
            sha256: fileHash,
            size: file.size,
            partition_size: chunkSize,
            content: base64Chunk,
            repo_uuid: '1ead87c8-424b-496c-959f-df1818722be3',
            partition_num: i,
            op: 1
        });

        const startTime = performance.now();

        try {
            await fetch('/file_json', {
                method: 'POST',
                headers: {
                    'Content-Type': 'application/json',
                },
                body: jsonPayload,
            });

            const endTime = performance.now();
            const timeTaken = (endTime - startTime) / 1000; // seconds
            const speed = (chunkSize / timeTaken / (1024 * 1024)).toFixed(2); // MB/s

            onSpeedUpdate(speed);
            onProgress(((i + 1) / totalChunks) * 100);

        } catch (error) {
            console.error('Error uploading chunk:', error);
            throw error;
        }
    }
}