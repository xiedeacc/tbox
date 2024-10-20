<template>
  <div id="app">
    <nav>
      <router-link to="/login">Login</router-link> |
      <router-link v-if="isAuthenticated" to="/dashboard">Dashboard</router-link> |
      <button v-if="isAuthenticated" @click="logout">Logout</button>
    </nav>
    <router-view />
  </div>
</template>

<script setup>
import { ref } from 'vue';
import { useRouter } from 'vue-router';

const isAuthenticated = ref(!!localStorage.getItem('token'));
const router = useRouter();

const logout = () => {
  localStorage.removeItem('auth');
  isAuthenticated.value = false;
  router.push('/login');
};
</script>

<style>
nav {
  padding: 1rem;
}
</style>