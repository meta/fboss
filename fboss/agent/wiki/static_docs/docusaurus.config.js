/**
 * (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.
 */

const lightCodeTheme = require('prism-react-renderer/themes/github');
const darkCodeTheme = require('prism-react-renderer/themes/dracula');
const {
  fbContent,
  fbInternalOnly
} = require('docusaurus-plugin-internaldocs-fb/internal');

// With JSDoc @type annotations, IDEs can provide config autocompletion
/** @type {import('@docusaurus/types').DocusaurusConfig} */
(module.exports = {
  title: 'FBOSS Agent',
  tagline: 'FBOSS Agent related docs reside here',
  url: 'https://internalfb.com',
  baseUrl: '/',
  onBrokenLinks: 'throw',
  onBrokenMarkdownLinks: 'throw',
  trailingSlash: true,
  favicon: 'img/favicon.ico',
  organizationName: 'meta',
  projectName: 'fboss_agent',
  i18n: {
    defaultLocale: 'en',
    locales: ['en']
  },
  customFields: {
    fbRepoName: 'fbsource',
    ossRepoPath: 'fbcode/fboss/agent/wiki/static_docs',
  },

  presets: [
    [
      'docusaurus-plugin-internaldocs-fb/docusaurus-preset',
      /** @type {import('docusaurus-plugin-internaldocs-fb').PresetOptions} */
      ({
        docs: {
          sidebarPath: require.resolve('./sidebars.js'),
          editUrl: fbContent({
            internal: 'https://www.internalfb.com/intern/diffusion/FBS/browse/master/fbcode/fboss/agent/wiki/static_docs',
            external: 'https://github.com/meta/fboss/tree/main/fboss/agent/wiki',
          }),
        },
        experimentalXRepoSnippets: {
          baseDir: '../../../..', // fbcode root
        },
        staticDocsProject: 'fboss_agent',
        trackingFile: 'fbcode/staticdocs/WATCHED_FILES',
        enableEditor: true,
        blog: false,
        theme: {
          customCss: require.resolve('./src/css/custom.css'),
        },
      }),
    ],
  ],

  themeConfig:
    /** @type {import('@docusaurus/preset-classic').ThemeConfig} */
    ({
      navbar: {
        title: 'FBOSS Agent',
        logo: {
          alt: 'FBOSS Agent',
          src: 'img/FBOSS_Agent.png',
          href: 'docs/',
        },
        items: fbInternalOnly([
        {
          to: 'docs/',
          label: 'Home',
          position: 'left',
        },
        {
          label: 'Docs',
          to: 'docs'
        },
      ]),
      },
      footer: {
        style: 'dark',
        links: [],
        copyright: `Copyright © ${new Date().getFullYear()} FBOSS Agent Docs`,
      },
      prism: {
        theme: lightCodeTheme,
        darkTheme: darkCodeTheme,
      },
    }),
});
