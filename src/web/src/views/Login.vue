<template>
  <div>
    <h2>Login</h2>
    <form @submit.prevent="login">
      <div>
        <label for="user">Username</label>
        <input v-model="user" id="user" type="text" required />
      </div>
      <div>
        <label for="password">Password</label>
        <input v-model="password" id="password" type="password" required />
      </div>
      <button type="submit">Login</button>
    </form>
    <p v-if="error">{{ error }}</p>
  </div>
</template>

<script setup>
import { ref } from 'vue';
import { useRouter } from 'vue-router';
import { v4 as uuidv4 } from 'uuid';  // Import uuid v4 generator

// State for username, password, and error messages
const user = ref('');
const password = ref('');
const error = ref(null);
const router = useRouter();

// Function to generate a unique request ID using uuid
const generateRequestId = () => uuidv4();

// Login function
const login = async () => {
  // Construct a JSON object with the username, password, and a unique request_id
  const userCredentials = {
    user: user.value,
    password: password.value,
    op: 3,
    request_id: generateRequestId(), // Generate unique request ID for each login request
  };

  try {
    // Send a POST request to the user endpoint (proxied by Nginx/dev server)
    const response = await fetch('/user', {
      method: 'POST',
      headers: {
        'Content-Type': 'application/json',
      },
      body: JSON.stringify(userCredentials),
    });

    // Check if the login was successful
    if (response.ok) {
      const data = await response.json();
      // Assuming the response contains a token or success message
      localStorage.setItem('user', user.value);
      localStorage.setItem('token', data.token);
      router.push('/dashboard');
    } else {
      const errorData = await response.json();
      error.value = errorData.message || 'Login failed. Please try again.';
    }
  } catch (err) {
    error.value = 'An error occurred while attempting to log in.';
  }
};
</script>

<style scoped>
form {
  margin-top: 1rem;
}
</style>