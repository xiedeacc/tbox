import axios from 'axios';
import { calculateLargeFileHash } from './utils.js';

export async function uploadFileInChunks(file, onProgress, onSpeedUpdate) {
    const chunkSize = 16 * 1024 * 1024; // 1MB
    const totalChunks = Math.ceil(file.size / chunkSize);
    const fileHash = await calculateLargeFileHash(file);
    // const fileHash = '4084ec48f2affbd501c02a942b674abcfdbbc6475070049de2c89fb6aa25a3f0'
    for (let i = 0; i < totalChunks; i++) {
        const start = i * chunkSize;
        const end = Math.min(start + chunkSize, file.size);
        const chunk = file.slice(start, end);

        // const chunkData = await readChunkAsArrayBuffer(chunk);
        const formData = new FormData();
        formData.append('path', file.name);
        formData.append('sha256', fileHash);
        formData.append('size', file.size);
        formData.append('partition_size', chunkSize);
        formData.append('content', chunk);
        formData.append('repo_uuid', '1ead87c8-424b-496c-959f-df1818722be3');
        formData.append('partition_num', i);
        formData.append('op', 1);
        const startTime = performance.now();
        try {
            await axios.post('/file', formData, {
                headers: {
                    'Content-Type': 'multipart/form-data',
                },
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