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
					label: 'Build the Demo',
					items: [
						{
							label: 'Create the Demo Project',
							slug: 'guides/creating-the-demo-project',
						},
						{
							label: 'Gameplay Classes and Input',
							slug: 'guides/creating-gameplay-classes-and-input',
						},
						{
							label: 'Create the First Level',
							slug: 'guides/creating-the-first-level',
						},
						{
							label: 'Create the Soldier',
							slug: 'guides/creating-the-soldier',
						},
						{
							label: 'Add Movement',
							slug: 'guides/adding-movement',
						},
						{ label: 'Add Combat', slug: 'guides/adding-combat' },
						{ label: 'Add Mineral Income', slug: 'guides/adding-mineral-income' },
						{ label: 'Build the HUD', slug: 'guides/building-the-hud' },
						{ label: 'Build Production Buildings', slug: 'guides/building-production-buildings' },
						{ label: 'Production and Research', slug: 'guides/adding-production-and-research' },
						{ label: 'Transport Truck', slug: 'guides/adding-the-transport-truck' },
						{ label: 'Fog and Minimap', slug: 'guides/adding-fog-and-a-minimap' },
						{ label: 'Skirmish Lobby', slug: 'guides/creating-the-skirmish-lobby' },
						{ label: 'Package and Verify', slug: 'guides/packaging-and-verifying-the-demo' },
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
