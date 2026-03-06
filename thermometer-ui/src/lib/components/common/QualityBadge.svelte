<script lang="ts">
  import type { Quality } from '../../api/types.ts';

  const { quality }: { quality: Quality } = $props();
</script>

<span class="badge {quality}">{quality}</span>
