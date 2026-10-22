#pragma once

static VkResult VKAPI_CALL PM_AllocateMemory(VkDevice d,const VkMemoryAllocateInfo *info,const VkAllocationCallbacks *a,VkDeviceMemory *result) {
PFN_vkAllocateMemory next=(PFN_vkAllocateMemory)dev_for(d)->gdpa(dev_for(d)->handle,"vkAllocateMemory");
struct stat st={0}; int imported=0;
if(pm_enabled) for(const VkBaseInStructure *p=info->pNext;p;p=p->pNext)
    if(p->sType==VK_STRUCTURE_TYPE_IMPORT_MEMORY_FD_INFO_KHR) {
        const VkImportMemoryFdInfoKHR *fd=(const void*)p;
        if(fstat(fd->fd,&st)==0) imported=1;else pm_gap(PM_META_GAP);
    }
VkResult r=next(d,info,a,result);
if(r==VK_SUCCESS && pm_enter(PM_META_GAP)) {
    struct pm_object *o=pm_put(PM_MEMORY,(uint64_t)*result,(uint64_t)d);
    if(o && imported) {o->a=st.st_ino;o->b=st.st_dev;}pm_leave();
}return r;
}

static VkResult VKAPI_CALL PM_GetMemoryFdKHR(VkDevice d,const VkMemoryGetFdInfoKHR *info,int *fd) {
PFN_vkGetMemoryFdKHR next=(PFN_vkGetMemoryFdKHR)dev_for(d)->gdpa(dev_for(d)->handle,"vkGetMemoryFdKHR");
VkResult r=next(d,info,fd);
if(r==VK_SUCCESS && pm_enabled) {
    struct stat st;
    if(fstat(*fd,&st)) pm_gap(PM_META_GAP);
    else if(pm_enter(PM_META_GAP)) {
        struct pm_object *o=pm_find(PM_MEMORY,(uint64_t)info->memory);
        if(!o)o=pm_put(PM_MEMORY,(uint64_t)info->memory,(uint64_t)d);
        if(o){o->a=st.st_ino;o->b=st.st_dev;}pm_leave();
    }
}return r;
}

static void VKAPI_CALL PM_FreeMemory(VkDevice d,VkDeviceMemory memory,const VkAllocationCallbacks *a) {
PFN_vkFreeMemory next=(PFN_vkFreeMemory)dev_for(d)->gdpa(dev_for(d)->handle,"vkFreeMemory");
pm_remove(PM_MEMORY,(uint64_t)memory);
next(d,memory,a);
}

static VkResult VKAPI_CALL PM_BindImageMemory(VkDevice d,VkImage image,VkDeviceMemory memory,VkDeviceSize offset) {
PFN_vkBindImageMemory next=(PFN_vkBindImageMemory)dev_for(d)->gdpa(dev_for(d)->handle,"vkBindImageMemory");
VkResult r=next(d,image,memory,offset);
if(r==VK_SUCCESS && pm_enter(PM_META_GAP)) {
struct pm_object *o=pm_put(PM_IMAGE,(uint64_t)image,(uint64_t)d);if(o){o->a=(uint64_t)memory;o->b=offset;}
pm_leave();}return r;
}

static VkResult VKAPI_CALL PM_BindImageMemory2(VkDevice d,uint32_t count,const VkBindImageMemoryInfo *info) {
PFN_vkBindImageMemory2 next=(PFN_vkBindImageMemory2)dev_for(d)->gdpa(dev_for(d)->handle,"vkBindImageMemory2");
if(!next) next=(PFN_vkBindImageMemory2)dev_for(d)->gdpa(dev_for(d)->handle,"vkBindImageMemory2KHR");
VkResult r=next(d,count,info);
if(r==VK_SUCCESS && pm_enter(PM_META_GAP)) {
for(uint32_t i=0;i<count;i++) { VkImage image=info[i].image;VkDeviceMemory memory=info[i].memory;VkDeviceSize offset=info[i].memoryOffset;
struct pm_object *o=pm_put(PM_IMAGE,(uint64_t)image,(uint64_t)d);if(o){o->a=(uint64_t)memory;o->b=offset;}
}
pm_leave();}return r;
}

static void VKAPI_CALL PM_DestroyImage(VkDevice d,VkImage image,const VkAllocationCallbacks *a) {
PFN_vkDestroyImage next=(PFN_vkDestroyImage)dev_for(d)->gdpa(dev_for(d)->handle,"vkDestroyImage");
pm_remove(PM_IMAGE,(uint64_t)image);
next(d,image,a);
}

static VkResult VKAPI_CALL PM_CreateImageView(VkDevice d,const VkImageViewCreateInfo *info,const VkAllocationCallbacks *a,VkImageView *view) {
PFN_vkCreateImageView next=(PFN_vkCreateImageView)dev_for(d)->gdpa(dev_for(d)->handle,"vkCreateImageView");
VkResult r=next(d,info,a,view);if(r==VK_SUCCESS && pm_enter(PM_META_GAP)) {
struct pm_object *o=pm_put(PM_VIEW,(uint64_t)*view,(uint64_t)d);if(o)o->a=(uint64_t)info->image;pm_leave();}return r;
}

static void VKAPI_CALL PM_DestroyImageView(VkDevice d,VkImageView view,const VkAllocationCallbacks *a) {
PFN_vkDestroyImageView next=(PFN_vkDestroyImageView)dev_for(d)->gdpa(dev_for(d)->handle,"vkDestroyImageView");
pm_remove(PM_VIEW,(uint64_t)view);
next(d,view,a);
}

static VkResult VKAPI_CALL PM_CreateFramebuffer(VkDevice d,const VkFramebufferCreateInfo *info,const VkAllocationCallbacks *a,VkFramebuffer *fb) {
PFN_vkCreateFramebuffer next=(PFN_vkCreateFramebuffer)dev_for(d)->gdpa(dev_for(d)->handle,"vkCreateFramebuffer");
VkResult r=next(d,info,a,fb);if(r==VK_SUCCESS && pm_enter(PM_META_GAP)) {
struct pm_object *o=pm_put(PM_FRAMEBUFFER,(uint64_t)*fb,(uint64_t)d);
if(o){o->a=info->flags&VK_FRAMEBUFFER_CREATE_IMAGELESS_BIT;
 if(info->attachmentCount>PM_ATTACHMENTS)pm_gap(PM_META_GAP);
 else if(!o->a) for(uint32_t i=0;i<info->attachmentCount;i++)o->refs[i]=(uint64_t)info->pAttachments[i];
}pm_leave();}return r;
}

static void VKAPI_CALL PM_DestroyFramebuffer(VkDevice d,VkFramebuffer fb,const VkAllocationCallbacks *a) {
PFN_vkDestroyFramebuffer next=(PFN_vkDestroyFramebuffer)dev_for(d)->gdpa(dev_for(d)->handle,"vkDestroyFramebuffer");
pm_remove(PM_FRAMEBUFFER,(uint64_t)fb);
next(d,fb,a);
}

static VkResult VKAPI_CALL PM_CreateRenderPass(VkDevice d,const VkRenderPassCreateInfo *info,const VkAllocationCallbacks *a,VkRenderPass *pass_) {
PFN_vkCreateRenderPass next=(PFN_vkCreateRenderPass)dev_for(d)->gdpa(dev_for(d)->handle,"vkCreateRenderPass");
VkResult r=next(d,info,a,pass_);if(r==VK_SUCCESS && pm_enter(PM_META_GAP)) {
struct pm_object *o=pm_put(PM_PASS,(uint64_t)*pass_,(uint64_t)d);
if(o && info->subpassCount<=PM_SUBPASSES && info->attachmentCount<=PM_ATTACHMENTS) {
 o->count=info->subpassCount;
 for(uint32_t i=0;i<info->attachmentCount;i++) if(info->pAttachments[i].loadOp==VK_ATTACHMENT_LOAD_OP_CLEAR)o->clears|=1u<<i;
 for(uint32_t i=0;i<info->subpassCount;i++) for(uint32_t j=0;j<info->pSubpasses[i].colorAttachmentCount;j++) {
  uint32_t k=info->pSubpasses[i].pColorAttachments[j].attachment;
  if(k<PM_ATTACHMENTS)o->colors[i]|=1u<<k;
  else if(k!=VK_ATTACHMENT_UNUSED)pm_gap(PM_META_GAP);
  if(info->pSubpasses[i].pResolveAttachments){k=info->pSubpasses[i].pResolveAttachments[j].attachment;
   if(k<PM_ATTACHMENTS)o->colors[i]|=1u<<k;else if(k!=VK_ATTACHMENT_UNUSED)pm_gap(PM_META_GAP);}
 }
}else pm_gap(PM_META_GAP);pm_leave();}return r;
}

static VkResult VKAPI_CALL PM_CreateRenderPass2(VkDevice d,const VkRenderPassCreateInfo2 *info,const VkAllocationCallbacks *a,VkRenderPass *pass_) {
PFN_vkCreateRenderPass2 next=(PFN_vkCreateRenderPass2)dev_for(d)->gdpa(dev_for(d)->handle,"vkCreateRenderPass2");
if(!next) next=(PFN_vkCreateRenderPass2)dev_for(d)->gdpa(dev_for(d)->handle,"vkCreateRenderPass2KHR");
VkResult r=next(d,info,a,pass_);if(r==VK_SUCCESS && pm_enter(PM_META_GAP)) {
struct pm_object *o=pm_put(PM_PASS,(uint64_t)*pass_,(uint64_t)d);
if(o && info->subpassCount<=PM_SUBPASSES && info->attachmentCount<=PM_ATTACHMENTS) {
 o->count=info->subpassCount;
 for(uint32_t i=0;i<info->attachmentCount;i++) if(info->pAttachments[i].loadOp==VK_ATTACHMENT_LOAD_OP_CLEAR)o->clears|=1u<<i;
 for(uint32_t i=0;i<info->subpassCount;i++) for(uint32_t j=0;j<info->pSubpasses[i].colorAttachmentCount;j++) {
  uint32_t k=info->pSubpasses[i].pColorAttachments[j].attachment;
  if(k<PM_ATTACHMENTS)o->colors[i]|=1u<<k;
  else if(k!=VK_ATTACHMENT_UNUSED)pm_gap(PM_META_GAP);
  if(info->pSubpasses[i].pResolveAttachments){k=info->pSubpasses[i].pResolveAttachments[j].attachment;
   if(k<PM_ATTACHMENTS)o->colors[i]|=1u<<k;else if(k!=VK_ATTACHMENT_UNUSED)pm_gap(PM_META_GAP);}
 }
}else pm_gap(PM_META_GAP);pm_leave();}return r;
}

static void VKAPI_CALL PM_DestroyRenderPass(VkDevice d,VkRenderPass pass_,const VkAllocationCallbacks *a) {
PFN_vkDestroyRenderPass next=(PFN_vkDestroyRenderPass)dev_for(d)->gdpa(dev_for(d)->handle,"vkDestroyRenderPass");
pm_remove(PM_PASS,(uint64_t)pass_);
next(d,pass_,a);
}

static VkResult VKAPI_CALL PM_BeginCommandBuffer(VkCommandBuffer cmd,const VkCommandBufferBeginInfo *info) {
PFN_vkBeginCommandBuffer next=(PFN_vkBeginCommandBuffer)dev_for(cmd)->gdpa(dev_for(cmd)->handle,"vkBeginCommandBuffer");
VkResult r=next(cmd,info);if(r==VK_SUCCESS && pm_enter(PM_COMMAND_GAP)) {
struct pm_object *c=pm_put(PM_COMMAND,(uint64_t)cmd,(uint64_t)dev_for(cmd)->handle);
if(c)c->recording_gap=pm_gap_count(PM_COMMAND_GAP);
if(c && (info->flags&VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT))c->b=1;
pm_leave();}return r;
}

static VkResult VKAPI_CALL PM_EndCommandBuffer(VkCommandBuffer cmd) {
PFN_vkEndCommandBuffer next=(PFN_vkEndCommandBuffer)dev_for(cmd)->gdpa(dev_for(cmd)->handle,"vkEndCommandBuffer");
VkResult r=next(cmd);
if(pm_enter(PM_COMMAND_GAP)) {
    struct pm_object *c=pm_find(PM_COMMAND,(uint64_t)cmd);
    if(c)c->ended=r==VK_SUCCESS && c->recording_gap==pm_gap_count(PM_COMMAND_GAP);
    else pm_gap(PM_COMMAND_GAP);
    pm_leave();
}return r;
}

static void VKAPI_CALL PM_FreeCommandBuffers(VkDevice d,VkCommandPool pool,uint32_t count,const VkCommandBuffer *cmds) {
PFN_vkFreeCommandBuffers next=(PFN_vkFreeCommandBuffers)dev_for(d)->gdpa(dev_for(d)->handle,"vkFreeCommandBuffers");
for(uint32_t i=0;i<count;i++)pm_remove(PM_COMMAND,(uint64_t)cmds[i]);
next(d,pool,count,cmds);
}

static void VKAPI_CALL PM_CmdBeginRenderPass(VkCommandBuffer cmd,const VkRenderPassBeginInfo *info,VkSubpassContents contents) {
PFN_vkCmdBeginRenderPass next=(PFN_vkCmdBeginRenderPass)dev_for(cmd)->gdpa(dev_for(cmd)->handle,"vkCmdBeginRenderPass");
pm_begin_pass(cmd,info);
next(cmd,info,contents);
}

static void VKAPI_CALL PM_CmdBeginRenderPass2(VkCommandBuffer cmd,const VkRenderPassBeginInfo *info,const VkSubpassBeginInfo *sub) {
PFN_vkCmdBeginRenderPass2 next=(PFN_vkCmdBeginRenderPass2)dev_for(cmd)->gdpa(dev_for(cmd)->handle,"vkCmdBeginRenderPass2");
if(!next) next=(PFN_vkCmdBeginRenderPass2)dev_for(cmd)->gdpa(dev_for(cmd)->handle,"vkCmdBeginRenderPass2KHR");
pm_begin_pass(cmd,info);
next(cmd,info,sub);
}

static void VKAPI_CALL PM_CmdNextSubpass(VkCommandBuffer cmd,VkSubpassContents contents) {
PFN_vkCmdNextSubpass next=(PFN_vkCmdNextSubpass)dev_for(cmd)->gdpa(dev_for(cmd)->handle,"vkCmdNextSubpass");
pm_next_pass(cmd);
next(cmd,contents);
}

static void VKAPI_CALL PM_CmdNextSubpass2(VkCommandBuffer cmd,const VkSubpassBeginInfo *begin,const VkSubpassEndInfo *end) {
PFN_vkCmdNextSubpass2 next=(PFN_vkCmdNextSubpass2)dev_for(cmd)->gdpa(dev_for(cmd)->handle,"vkCmdNextSubpass2");
if(!next) next=(PFN_vkCmdNextSubpass2)dev_for(cmd)->gdpa(dev_for(cmd)->handle,"vkCmdNextSubpass2KHR");
pm_next_pass(cmd);
next(cmd,begin,end);
}

static void VKAPI_CALL PM_CmdEndRenderPass(VkCommandBuffer cmd) {
PFN_vkCmdEndRenderPass next=(PFN_vkCmdEndRenderPass)dev_for(cmd)->gdpa(dev_for(cmd)->handle,"vkCmdEndRenderPass");
pm_end_pass(cmd);
next(cmd);
}

static void VKAPI_CALL PM_CmdEndRenderPass2(VkCommandBuffer cmd,const VkSubpassEndInfo *end) {
PFN_vkCmdEndRenderPass2 next=(PFN_vkCmdEndRenderPass2)dev_for(cmd)->gdpa(dev_for(cmd)->handle,"vkCmdEndRenderPass2");
if(!next) next=(PFN_vkCmdEndRenderPass2)dev_for(cmd)->gdpa(dev_for(cmd)->handle,"vkCmdEndRenderPass2KHR");
pm_end_pass(cmd);
next(cmd,end);
}

static void VKAPI_CALL PM_CmdDraw(VkCommandBuffer cmd,uint32_t vertices,uint32_t instances,uint32_t first,uint32_t first_instance) {
PFN_vkCmdDraw next=(PFN_vkCmdDraw)dev_for(cmd)->gdpa(dev_for(cmd)->handle,"vkCmdDraw");
if(vertices && instances)pm_draw(cmd);
next(cmd,vertices,instances,first,first_instance);
}

static void VKAPI_CALL PM_CmdDrawIndexed(VkCommandBuffer cmd,uint32_t indices,uint32_t instances,uint32_t first,int32_t offset,uint32_t first_instance) {
PFN_vkCmdDrawIndexed next=(PFN_vkCmdDrawIndexed)dev_for(cmd)->gdpa(dev_for(cmd)->handle,"vkCmdDrawIndexed");
if(indices && instances)pm_draw(cmd);
next(cmd,indices,instances,first,offset,first_instance);
}

static void VKAPI_CALL PM_CmdDrawIndirect(VkCommandBuffer cmd,VkBuffer buffer,VkDeviceSize offset,uint32_t count,uint32_t stride) {
PFN_vkCmdDrawIndirect next=(PFN_vkCmdDrawIndirect)dev_for(cmd)->gdpa(dev_for(cmd)->handle,"vkCmdDrawIndirect");
if(count)pm_draw(cmd);
next(cmd,buffer,offset,count,stride);
}

static void VKAPI_CALL PM_CmdDrawIndexedIndirect(VkCommandBuffer cmd,VkBuffer buffer,VkDeviceSize offset,uint32_t count,uint32_t stride) {
PFN_vkCmdDrawIndexedIndirect next=(PFN_vkCmdDrawIndexedIndirect)dev_for(cmd)->gdpa(dev_for(cmd)->handle,"vkCmdDrawIndexedIndirect");
if(count)pm_draw(cmd);
next(cmd,buffer,offset,count,stride);
}

static void VKAPI_CALL PM_CmdClearAttachments(VkCommandBuffer cmd,uint32_t count,const VkClearAttachment *attachments,uint32_t rect_count,const VkClearRect *rects) {
PFN_vkCmdClearAttachments next=(PFN_vkCmdClearAttachments)dev_for(cmd)->gdpa(dev_for(cmd)->handle,"vkCmdClearAttachments");
if(count && rect_count)pm_draw(cmd);
next(cmd,count,attachments,rect_count,rects);
}

static void VKAPI_CALL PM_CmdCopyImage(VkCommandBuffer cmd,VkImage src,VkImageLayout sl,VkImage dst,VkImageLayout dl,uint32_t count,const VkImageCopy *regions) {
PFN_vkCmdCopyImage next=(PFN_vkCmdCopyImage)dev_for(cmd)->gdpa(dev_for(cmd)->handle,"vkCmdCopyImage");
if(count)pm_write(cmd,dst,PM_COPY);
next(cmd,src,sl,dst,dl,count,regions);
}

static void VKAPI_CALL PM_CmdBlitImage(VkCommandBuffer cmd,VkImage src,VkImageLayout sl,VkImage dst,VkImageLayout dl,uint32_t count,const VkImageBlit *regions,VkFilter filter) {
PFN_vkCmdBlitImage next=(PFN_vkCmdBlitImage)dev_for(cmd)->gdpa(dev_for(cmd)->handle,"vkCmdBlitImage");
if(count)pm_write(cmd,dst,PM_BLIT);
next(cmd,src,sl,dst,dl,count,regions,filter);
}

static void VKAPI_CALL PM_CmdResolveImage(VkCommandBuffer cmd,VkImage src,VkImageLayout sl,VkImage dst,VkImageLayout dl,uint32_t count,const VkImageResolve *regions) {
PFN_vkCmdResolveImage next=(PFN_vkCmdResolveImage)dev_for(cmd)->gdpa(dev_for(cmd)->handle,"vkCmdResolveImage");
if(count)pm_write(cmd,dst,PM_RESOLVE);
next(cmd,src,sl,dst,dl,count,regions);
}

static void VKAPI_CALL PM_CmdCopyBufferToImage(VkCommandBuffer cmd,VkBuffer src,VkImage dst,VkImageLayout dl,uint32_t count,const VkBufferImageCopy *regions) {
PFN_vkCmdCopyBufferToImage next=(PFN_vkCmdCopyBufferToImage)dev_for(cmd)->gdpa(dev_for(cmd)->handle,"vkCmdCopyBufferToImage");
if(count)pm_write(cmd,dst,PM_COPY);
next(cmd,src,dst,dl,count,regions);
}

static void VKAPI_CALL PM_CmdClearColorImage(VkCommandBuffer cmd,VkImage image,VkImageLayout layout,const VkClearColorValue *color,uint32_t count,const VkImageSubresourceRange *ranges) {
PFN_vkCmdClearColorImage next=(PFN_vkCmdClearColorImage)dev_for(cmd)->gdpa(dev_for(cmd)->handle,"vkCmdClearColorImage");
if(count)pm_write(cmd,image,PM_CLEAR);
next(cmd,image,layout,color,count,ranges);
}

static void VKAPI_CALL PM_CmdCopyImage2(VkCommandBuffer cmd,const VkCopyImageInfo2 *info) {
PFN_vkCmdCopyImage2 next=(PFN_vkCmdCopyImage2)dev_for(cmd)->gdpa(dev_for(cmd)->handle,"vkCmdCopyImage2");
if(!next) next=(PFN_vkCmdCopyImage2)dev_for(cmd)->gdpa(dev_for(cmd)->handle,"vkCmdCopyImage2KHR");
if(info->regionCount)pm_write(cmd,info->dstImage,PM_COPY);
next(cmd,info);
}

static void VKAPI_CALL PM_CmdBlitImage2(VkCommandBuffer cmd,const VkBlitImageInfo2 *info) {
PFN_vkCmdBlitImage2 next=(PFN_vkCmdBlitImage2)dev_for(cmd)->gdpa(dev_for(cmd)->handle,"vkCmdBlitImage2");
if(!next) next=(PFN_vkCmdBlitImage2)dev_for(cmd)->gdpa(dev_for(cmd)->handle,"vkCmdBlitImage2KHR");
if(info->regionCount)pm_write(cmd,info->dstImage,PM_BLIT);
next(cmd,info);
}

static void VKAPI_CALL PM_CmdResolveImage2(VkCommandBuffer cmd,const VkResolveImageInfo2 *info) {
PFN_vkCmdResolveImage2 next=(PFN_vkCmdResolveImage2)dev_for(cmd)->gdpa(dev_for(cmd)->handle,"vkCmdResolveImage2");
if(!next) next=(PFN_vkCmdResolveImage2)dev_for(cmd)->gdpa(dev_for(cmd)->handle,"vkCmdResolveImage2KHR");
if(info->regionCount)pm_write(cmd,info->dstImage,PM_RESOLVE);
next(cmd,info);
}

static void VKAPI_CALL PM_CmdCopyBufferToImage2(VkCommandBuffer cmd,const VkCopyBufferToImageInfo2 *info) {
PFN_vkCmdCopyBufferToImage2 next=(PFN_vkCmdCopyBufferToImage2)dev_for(cmd)->gdpa(dev_for(cmd)->handle,"vkCmdCopyBufferToImage2");
if(!next) next=(PFN_vkCmdCopyBufferToImage2)dev_for(cmd)->gdpa(dev_for(cmd)->handle,"vkCmdCopyBufferToImage2KHR");
if(info->regionCount)pm_write(cmd,info->dstImage,PM_COPY);
next(cmd,info);
}

static void VKAPI_CALL PM_CmdBeginRendering(VkCommandBuffer cmd,const VkRenderingInfo *info) {
PFN_vkCmdBeginRendering next=(PFN_vkCmdBeginRendering)dev_for(cmd)->gdpa(dev_for(cmd)->handle,"vkCmdBeginRendering");
if(!next) next=(PFN_vkCmdBeginRendering)dev_for(cmd)->gdpa(dev_for(cmd)->handle,"vkCmdBeginRenderingKHR");
if(pm_enter(PM_COMMAND_GAP)) {
struct pm_object *c=pm_find(PM_COMMAND,(uint64_t)cmd);
if(!c || info->colorAttachmentCount>PM_ATTACHMENTS/2)pm_gap(PM_COMMAND_GAP);
else {
 c->active_count=0;c->b=0;
 for(uint32_t i=0;i<info->colorAttachmentCount;i++) {
  const VkRenderingAttachmentInfo *a=&info->pColorAttachments[i];if(!a->imageView)continue;
  uint64_t image=pm_view_image((uint64_t)a->imageView);c->active[c->active_count++]=image;
  if(a->loadOp==VK_ATTACHMENT_LOAD_OP_CLEAR)pm_write_locked(c,image,PM_LOAD_CLEAR);
  if(a->resolveMode && a->resolveImageView)c->active[c->active_count++]=pm_view_image((uint64_t)a->resolveImageView);
 }
}pm_leave();}next(cmd,info);
}

static void VKAPI_CALL PM_CmdEndRendering(VkCommandBuffer cmd) {
PFN_vkCmdEndRendering next=(PFN_vkCmdEndRendering)dev_for(cmd)->gdpa(dev_for(cmd)->handle,"vkCmdEndRendering");
if(!next) next=(PFN_vkCmdEndRendering)dev_for(cmd)->gdpa(dev_for(cmd)->handle,"vkCmdEndRenderingKHR");
pm_end_pass(cmd);
next(cmd);
}

static void VKAPI_CALL PM_CmdExecuteCommands(VkCommandBuffer cmd,uint32_t count,const VkCommandBuffer *children) {
PFN_vkCmdExecuteCommands next=(PFN_vkCmdExecuteCommands)dev_for(cmd)->gdpa(dev_for(cmd)->handle,"vkCmdExecuteCommands");
if(pm_enter(PM_COMMAND_GAP)) {
struct pm_object *c=pm_find(PM_COMMAND,(uint64_t)cmd);
for(uint32_t i=0;i<count;i++) {
 struct pm_object *child=pm_find(PM_COMMAND,(uint64_t)children[i]);
 if(!c || !child || !child->ended || child->recording_gap!=pm_gap_count(PM_COMMAND_GAP)) {pm_gap(PM_COMMAND_GAP);continue;}
 for(unsigned j=0;j<child->count;j++)pm_write_locked(c,child->writes[j].image,child->writes[j].kinds);
}pm_leave();}next(cmd,count,children);
}

static void VKAPI_CALL PM_CmdDrawIndirectCount(VkCommandBuffer cmd,VkBuffer buffer,VkDeviceSize offset,
        VkBuffer count_buffer,VkDeviceSize count_offset,uint32_t max_count,uint32_t stride) {
    PFN_vkCmdDrawIndirectCount next=(PFN_vkCmdDrawIndirectCount)dev_for(cmd)->gdpa(dev_for(cmd)->handle,"vkCmdDrawIndirectCount");
    if(!next)next=(PFN_vkCmdDrawIndirectCount)dev_for(cmd)->gdpa(dev_for(cmd)->handle,"vkCmdDrawIndirectCountKHR");
    if(max_count)pm_draw(cmd);
    next(cmd,buffer,offset,count_buffer,count_offset,max_count,stride);
}
static void VKAPI_CALL PM_CmdDrawIndexedIndirectCount(VkCommandBuffer cmd,VkBuffer buffer,VkDeviceSize offset,
        VkBuffer count_buffer,VkDeviceSize count_offset,uint32_t max_count,uint32_t stride) {
    PFN_vkCmdDrawIndexedIndirectCount next=(PFN_vkCmdDrawIndexedIndirectCount)dev_for(cmd)->gdpa(dev_for(cmd)->handle,"vkCmdDrawIndexedIndirectCount");
    if(!next)next=(PFN_vkCmdDrawIndexedIndirectCount)dev_for(cmd)->gdpa(dev_for(cmd)->handle,"vkCmdDrawIndexedIndirectCountKHR");
    if(max_count)pm_draw(cmd);
    next(cmd,buffer,offset,count_buffer,count_offset,max_count,stride);
}

static PFN_vkVoidFunction pm_hook(const char *name) {
if(!strcmp(name,"vkEndCommandBuffer"))return (PFN_vkVoidFunction)PM_EndCommandBuffer;
if(!strcmp(name,"vkCmdDrawIndirectCount"))return (PFN_vkVoidFunction)PM_CmdDrawIndirectCount;
if(!strcmp(name,"vkCmdDrawIndirectCountKHR"))return (PFN_vkVoidFunction)PM_CmdDrawIndirectCount;
if(!strcmp(name,"vkCmdDrawIndexedIndirectCount"))return (PFN_vkVoidFunction)PM_CmdDrawIndexedIndirectCount;
if(!strcmp(name,"vkCmdDrawIndexedIndirectCountKHR"))return (PFN_vkVoidFunction)PM_CmdDrawIndexedIndirectCount;


if(!strcmp(name,"vkAllocateMemory"))return (PFN_vkVoidFunction)PM_AllocateMemory;

if(!strcmp(name,"vkGetMemoryFdKHR"))return (PFN_vkVoidFunction)PM_GetMemoryFdKHR;

if(!strcmp(name,"vkFreeMemory"))return (PFN_vkVoidFunction)PM_FreeMemory;

if(!strcmp(name,"vkBindImageMemory"))return (PFN_vkVoidFunction)PM_BindImageMemory;

if(!strcmp(name,"vkBindImageMemory2"))return (PFN_vkVoidFunction)PM_BindImageMemory2;

if(!strcmp(name,"vkBindImageMemory2KHR"))return (PFN_vkVoidFunction)PM_BindImageMemory2;

if(!strcmp(name,"vkDestroyImage"))return (PFN_vkVoidFunction)PM_DestroyImage;

if(!strcmp(name,"vkCreateImageView"))return (PFN_vkVoidFunction)PM_CreateImageView;

if(!strcmp(name,"vkDestroyImageView"))return (PFN_vkVoidFunction)PM_DestroyImageView;

if(!strcmp(name,"vkCreateFramebuffer"))return (PFN_vkVoidFunction)PM_CreateFramebuffer;

if(!strcmp(name,"vkDestroyFramebuffer"))return (PFN_vkVoidFunction)PM_DestroyFramebuffer;

if(!strcmp(name,"vkCreateRenderPass"))return (PFN_vkVoidFunction)PM_CreateRenderPass;

if(!strcmp(name,"vkCreateRenderPass2"))return (PFN_vkVoidFunction)PM_CreateRenderPass2;

if(!strcmp(name,"vkCreateRenderPass2KHR"))return (PFN_vkVoidFunction)PM_CreateRenderPass2;

if(!strcmp(name,"vkDestroyRenderPass"))return (PFN_vkVoidFunction)PM_DestroyRenderPass;

if(!strcmp(name,"vkBeginCommandBuffer"))return (PFN_vkVoidFunction)PM_BeginCommandBuffer;

if(!strcmp(name,"vkFreeCommandBuffers"))return (PFN_vkVoidFunction)PM_FreeCommandBuffers;

if(!strcmp(name,"vkCmdBeginRenderPass"))return (PFN_vkVoidFunction)PM_CmdBeginRenderPass;

if(!strcmp(name,"vkCmdBeginRenderPass2"))return (PFN_vkVoidFunction)PM_CmdBeginRenderPass2;

if(!strcmp(name,"vkCmdBeginRenderPass2KHR"))return (PFN_vkVoidFunction)PM_CmdBeginRenderPass2;

if(!strcmp(name,"vkCmdNextSubpass"))return (PFN_vkVoidFunction)PM_CmdNextSubpass;

if(!strcmp(name,"vkCmdNextSubpass2"))return (PFN_vkVoidFunction)PM_CmdNextSubpass2;

if(!strcmp(name,"vkCmdNextSubpass2KHR"))return (PFN_vkVoidFunction)PM_CmdNextSubpass2;

if(!strcmp(name,"vkCmdEndRenderPass"))return (PFN_vkVoidFunction)PM_CmdEndRenderPass;

if(!strcmp(name,"vkCmdEndRenderPass2"))return (PFN_vkVoidFunction)PM_CmdEndRenderPass2;

if(!strcmp(name,"vkCmdEndRenderPass2KHR"))return (PFN_vkVoidFunction)PM_CmdEndRenderPass2;

if(!strcmp(name,"vkCmdDraw"))return (PFN_vkVoidFunction)PM_CmdDraw;

if(!strcmp(name,"vkCmdDrawIndexed"))return (PFN_vkVoidFunction)PM_CmdDrawIndexed;

if(!strcmp(name,"vkCmdDrawIndirect"))return (PFN_vkVoidFunction)PM_CmdDrawIndirect;

if(!strcmp(name,"vkCmdDrawIndexedIndirect"))return (PFN_vkVoidFunction)PM_CmdDrawIndexedIndirect;

if(!strcmp(name,"vkCmdClearAttachments"))return (PFN_vkVoidFunction)PM_CmdClearAttachments;

if(!strcmp(name,"vkCmdCopyImage"))return (PFN_vkVoidFunction)PM_CmdCopyImage;

if(!strcmp(name,"vkCmdBlitImage"))return (PFN_vkVoidFunction)PM_CmdBlitImage;

if(!strcmp(name,"vkCmdResolveImage"))return (PFN_vkVoidFunction)PM_CmdResolveImage;

if(!strcmp(name,"vkCmdCopyBufferToImage"))return (PFN_vkVoidFunction)PM_CmdCopyBufferToImage;

if(!strcmp(name,"vkCmdClearColorImage"))return (PFN_vkVoidFunction)PM_CmdClearColorImage;

if(!strcmp(name,"vkCmdCopyImage2"))return (PFN_vkVoidFunction)PM_CmdCopyImage2;

if(!strcmp(name,"vkCmdCopyImage2KHR"))return (PFN_vkVoidFunction)PM_CmdCopyImage2;

if(!strcmp(name,"vkCmdBlitImage2"))return (PFN_vkVoidFunction)PM_CmdBlitImage2;

if(!strcmp(name,"vkCmdBlitImage2KHR"))return (PFN_vkVoidFunction)PM_CmdBlitImage2;

if(!strcmp(name,"vkCmdResolveImage2"))return (PFN_vkVoidFunction)PM_CmdResolveImage2;

if(!strcmp(name,"vkCmdResolveImage2KHR"))return (PFN_vkVoidFunction)PM_CmdResolveImage2;

if(!strcmp(name,"vkCmdCopyBufferToImage2"))return (PFN_vkVoidFunction)PM_CmdCopyBufferToImage2;

if(!strcmp(name,"vkCmdCopyBufferToImage2KHR"))return (PFN_vkVoidFunction)PM_CmdCopyBufferToImage2;

if(!strcmp(name,"vkCmdBeginRendering"))return (PFN_vkVoidFunction)PM_CmdBeginRendering;

if(!strcmp(name,"vkCmdBeginRenderingKHR"))return (PFN_vkVoidFunction)PM_CmdBeginRendering;

if(!strcmp(name,"vkCmdEndRendering"))return (PFN_vkVoidFunction)PM_CmdEndRendering;

if(!strcmp(name,"vkCmdEndRenderingKHR"))return (PFN_vkVoidFunction)PM_CmdEndRendering;

if(!strcmp(name,"vkCmdExecuteCommands"))return (PFN_vkVoidFunction)PM_CmdExecuteCommands;

return NULL;
}
