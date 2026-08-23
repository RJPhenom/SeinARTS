// @ts-check
import { defineConfig } from 'astro/config';
import sitemap from '@astrojs/sitemap';
import starlight from '@astrojs/starlight';

const siteDescription =
	'A modular, deterministic lockstep RTS framework for Unreal Engine 5.';
const isProduction = process.env.NODE_ENV === 'production';

export default defineConfig({
	site: 'https://docs.seinarts.gg',
	integrations: [
		starlight({
			title: 'SeinARTS Documentation',
			description: siteDescription,
			components: {
				Footer: './src/components/Footer.astro',
				Header: './src/components/Header.astro',
				MobileTableOfContents: './src/components/MobileTableOfContents.astro',
				ThemeProvider: './src/components/ThemeProvider.astro',
				ThemeSelect: './src/components/ThemeSelect.astro',
			},
			favicon: '/favicon.svg',
			head: [
				{ tag: 'link', attrs: { rel: 'icon', href: '/favicon.ico', sizes: '16x16 32x32 48x48' } },
				{ tag: 'link', attrs: { rel: 'apple-touch-icon', href: '/apple-touch-icon.png', sizes: '180x180' } },
			],
			logo: {
				src: '../Plugins/SeinARTSFramework/Resources/BrandKit/SeinARTSWordmarkVectorized.svg',
				alt: 'SeinARTS Framework',
				replacesTitle: true,
			},
			customCss: ['./src/styles/seinarts.css'],
			social: [
				{
					icon: 'github',
					label: 'SeinARTS on GitHub',
					href: 'https://github.com/RJPhenom/SeinARTS',
				},
			],
			editLink: {
				baseUrl: 'https://github.com/RJPhenom/SeinARTS/edit/main/Docs/',
			},
			lastUpdated: isProduction,
			expressiveCode: {
				themes: ['starlight-dark'],
				useStarlightDarkModeSwitch: false,
			},
			sidebar: [
				{
					label: 'Start Here',
					items: [
						{ label: 'Getting Started', slug: 'getting-started' },
						{ label: 'Plugin Ecosystem', slug: 'ecosystem' },
					],
				},
				{
					label: 'Guides',
					items: [
						{
							label: 'Creating an Entity Blueprint',
							slug: 'guides/creating-an-entity-blueprint',
						},
						{
							label: 'Making an Infantry Unit',
							slug: 'guides/making-an-infantry-unit',
						},
						{
							label: 'Making a Movement Ability',
							slug: 'guides/making-a-movement-ability',
						},
						{
							label: 'Making Combat Abilities',
							slug: 'guides/making-combat-abilities',
						},
						{
							label: 'Setting Up a Playable Level',
							slug: 'guides/setting-up-a-playable-level',
						},
					],
				},
				{
					label: 'Core Concepts',
					items: [
						{
							label: 'Deterministic Simulation',
							slug: 'core-concepts/deterministic-simulation',
						},
						{
							label: 'Units, Components, and Abilities',
							slug: 'core-concepts/units-components-abilities',
						},
					],
				},
			],
		}),
		sitemap(),
	],
});
