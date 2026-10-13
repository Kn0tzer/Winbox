// vkd3d uses Wine severity fields in its console records.
export const isVkd3dError = line => /\b(?:err|error|fatal):vkd3d(?:-proton)?:/i.test(line);
