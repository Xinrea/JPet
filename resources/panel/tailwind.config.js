import flowbitePlugin from 'flowbite/plugin'

export default {
	content: ['./src/**/*.{html,js,svelte,ts}', './node_modules/flowbite-svelte/**/*.{html,js,svelte,ts}'],

	theme: {
		extend: {
      colors: {
        // flowbite-svelte
		    primary: {
		        '50': '#f4fbea',
		        '100': '#e5f7d0',
		        '200': '#cdefa7',
		        '300': '#ace274',
		        '400': '#8dd348',
		        '500': '#79ca2e',
		        '600': '#53931d',
		        '700': '#40701b',
		        '800': '#36591b',
		        '900': '#2f4c1b',
		        '950': '#162a09',
		    },
      }
    }
	},

	plugins: [flowbitePlugin]
};
