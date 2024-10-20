<template>
  <div>
    <h2>Register</h2>
    <form @submit.prevent="register">
      <div>
        <label for="username">Username</label>
        <input v-model="username" id="username" type="text" required />
      </div>
      <div>
        <label for="password">Password</label>
        <input v-model="password" id="password" type="password" required />
      </div>
      <button type="submit">Register</button>
    </form>
    <p v-if="error">{{ error }}</p>
  </div>
</template>

<script setup>
import { ref } from 'vue';
import { useRouter } from 'vue-router';

const username = ref('');
const password = ref('');
const error = ref(null);
const router = useRouter();

const register = () => {
  if (localStorage.getItem(username.value)) {
    error.value = 'User already exists.';
  } else {
    localStorage.setItem(username.value, password.value);
    localStorage.setItem('auth', username.value);
    router.push('/dashboard');
  }
};
</script>

<style scoped>
form {
  margin-top: 1rem;
}
</style>