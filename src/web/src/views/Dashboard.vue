<template>
  <div>
    <h2>Welcome to the Dashboard, {{ user }}</h2>
    <div v-if="ipAddrs">
      <h3>Server Response:</h3>
      <pre>{{ ipAddrs }}</pre>
    </div>
    <p v-if="error">{{ error }}</p>
  </div>
</template>

<script setup>
import { ref, onMounted } from 'vue';
import { v4 as uuidv4 } from 'uuid';  // Import uuid v4 generator

// Fetch user and token from localStorage
const user = ref(localStorage.getItem('user'));
const token = ref(localStorage.getItem('token'));
const ipAddrs = ref(null);
const error = ref(null);
const generateRequestId = () => uuidv4();

// Function to fetch data from the server
const fetchServerData = async () => {
  try {
    // Simple GET to backend ServerHandler; Nginx/dev proxy will route to /server
    const response = await fetch('/server', { method: 'GET' });

    if (response.ok) {
      const data = await response.json();
      // Expecting shape: { client_ip: "x.x.x.x" }
      ipAddrs.value = JSON.stringify(data, null, 2);
    } else {
      error.value = 'Failed to fetch server data.';
    }
  } catch (err) {
    error.value = 'An error occurred while fetching server data.';
  }
};

// Fetch data when the component is mounted
onMounted(() => {
  fetchServerData(); // Call the fetch function once the component is mounted
});
</script>

<style scoped>
h2 {
  margin-top: 1rem;
}
pre {
  background-color: #f4f4f4;
  padding: 10px;
  border-radius: 5px;
  white-space: pre-wrap; /* Display the response nicely */
}
</style>