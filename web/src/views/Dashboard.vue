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
    // Construct a request object with user credentials
    const requestData = {
      user: user.value,
      token: token.value,
      op: 5,
      request_id: generateRequestId(),
    };

    // Send a POST request to the server
    const response = await fetch('https://ip.xiedeacc.com/proxygen/server', {
      method: 'POST',
      headers: {
        'Content-Type': 'application/json',
      },
      body: JSON.stringify(requestData),
    });

    // Check if the request was successful
    if (response.ok) {
     const data = await response.json();

      // Check if the `err_msg` contains a stringified JSON object
      if (data.err_msg) {
        try {
          // Parse the stringified JSON inside err_msg
          const parsedMsg = JSON.parse(data.err_msg);
          ipAddrs.value = JSON.stringify(parsedMsg, null, 2); // Pretty-print the parsed JSON
        } catch (parseError) {
          // If parsing fails, display the raw message
          ipAddrs.value = data.err_msg;
        }
      } else {
        ipAddrs.value = JSON.stringify(data, null, 2); // Pretty-print the entire response if no err_msg exists
      }
    } else {
      const errorData = await response.json();
      error.value = errorData.message || 'Failed to fetch server data.';
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