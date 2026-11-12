/**
 * (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.
 */

/* eslint-disable */

const lightCodeTheme = require('prism-react-renderer/themes/github');
const darkCodeTheme = require('prism-react-renderer/themes/dracula');

// With JSDoc @type annotations, IDEs can provide config autocompletion
/** @type {import('@docusaurus/types').DocusaurusConfig} */
(module.exports = {
  title: 'FBOSS Documentation', // TODO
  tagline: 'https://github.com/meta/fboss', // TODO
  url: 'https://internalfb.com',
  baseUrl: '/fboss/',
  onBrokenLinks: 'throw',
  onBrokenMarkdownLinks: 'throw',
  trailingSlash: true,
  favicon: 'img/favicon.ico',
  organizationName: 'meta',
  projectName: 'fboss', // TODO
  deploymentBranch: 'main',
  customFields: {
    fbRepoName: 'fbsource',
    ossRepoPath: 'fbcode/fboss/github/docs',
  },

  presets: [
    [
      'docusaurus-plugin-internaldocs-fb/docusaurus-preset',
      /** @type {import('docusaurus-plugin-internaldocs-fb').PresetOptions} */
      ({
        docs: {
          sidebarPath: require.resolve('./sidebars.js'),
          editUrl: 'https://www.internalfb.com/code/fbsource/fbcode/fboss/github/docs', // TODO Please change this to your repo.
        },
        experimentalXRepoSnippets: {
          baseDir: '.',
        },
        staticDocsProject: 'fboss',
        trackingFile: 'fbcode/staticdocs/WATCHED_FILES',
        blog: {
          showReadingTime: true,
          editUrl: 'https://www.internalfb.com/code/fbsource/fbcode/fboss/github/docs', // TODO change to path to your project in fbsource/www
        },
        theme: {
          customCss: require.resolve('./src/css/custom.css'),
        },
      }),
    ],
  ],

  plugins: [
    require.resolve('./src/plugins/changelog'),
  ],

  themeConfig:
    /** @type {import('@docusaurus/preset-classic').ThemeConfig} */
    ({
      navbar: {
        title: 'fboss',
        logo: {
          alt: 'fboss Logo',
          src: 'img/logo.svg',
        },
        items: [
          {label: 'Onboard', position: 'left', docId: 'onboarding/platform_onboarding', type: 'doc'},
          {label: 'Manuals', position: 'left', docId: 'manuals/add_initial_support_for_platform_services', type: 'doc'},
          {label: 'Build', position: 'left', docId: 'build/building_fboss_on_docker_containers', type: 'doc'},
          {label: 'Develop', position: 'left', docId: 'developing/platform_mapping', type: 'doc'},
          {label: 'Test', position: 'left', docId: 'testing/test_categories', type: 'doc'},
          {label: 'Debug', position: 'left', docId: 'debugging/clis_for_l1_debugging', type: 'doc'},
          {label: 'Lab Distro', position: 'left', docId: 'lab_distro/distro_image_manifest', type: 'doc'},
          {label: 'Architecture', position: 'left', docId: 'architecture/meta_switch_architecture', type: 'doc'},
          {label: 'Test Results', position: 'left', docId: 'test_results/test_results', type: 'doc'},
          {label: 'Changelog', to: '/changelog', position: 'left'},
          {
            href: 'https://github.com/meta/fboss',
            label: 'GitHub',
            position: 'right',
          },
        ],
      },
      footer: {
        style: 'dark',
        /*
        links: [
          {
            title: 'Docs',
            items: [
              {
                label: 'Tutorial',
                to: '/docs/intro',
              },
            ],
          },
          {
            title: 'Community',
            items: [
              {
                label: 'Stack Overflow',
                href: 'https://stackoverflow.com/questions/tagged/docusaurus',
              },
              {
                label: 'Discord',
                href: 'https://discordapp.com/invite/docusaurus',
              },
              {
                label: 'Twitter',
                href: 'https://twitter.com/docusaurus',
              },
            ],
          },
          {
            title: 'More',
            items: [
              {
                label: 'Blog',
                to: '/blog',
              },
              {
                label: 'GitHub',
                href: 'https://github.com/facebook/docusaurus',
              },
            ],
          },
        ],
        copyright: `Copyright © ${new Date().getFullYear()} My Project, Inc. Built with Docusaurus.`,
        */
        copyright: `Copyright © 2004-Present Facebook. All Rights Reserved.`,
      },
      prism: {
        theme: lightCodeTheme,
        darkTheme: darkCodeTheme,
      },
    }),
});
