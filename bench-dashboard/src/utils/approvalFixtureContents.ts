const fixtureModules = import.meta.glob('../../../tests/Approval/fixtures/*.approved.txt', {
  query: '?raw',
  import: 'default',
  eager: true,
}) as Record<string, string>;

export const approvalFixtureContents = Object.fromEntries(
  Object.entries(fixtureModules).map(([path, content]) => {
    const fileName = path.slice(path.lastIndexOf('/') + 1);
    return [`tests/Approval/fixtures/${fileName}`, content];
  }),
) as Record<string, string>;
